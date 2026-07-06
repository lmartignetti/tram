#!/usr/bin/bash

set -euo pipefail
IFS=$'\n\t'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_FOLDER="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONFIG_TEST_DIR="build/config"
CONFIG_NETWORK="${SCRIPT_DIR}/../build/config/network.json"
BASE_LOG_DIR="${PROJECT_FOLDER}/logs"
CONFIG_TESTS="build/config/tests.conf"
USER_DEFAULT="ubuntu"

# Helper functions
die() {
    echo "❌ $*" >&2
    exit 1
}
info() { echo "ℹ️  $*"; }
success() { echo "✅ $*"; }
warn() { echo "⚠️  $*"; }

# Argument parsing
BUILD_TYPE="" # one of Release, Profiling, Debug, Valgrind
LOCAL=false
SSH_USER="${USER_DEFAULT}"

while [[ $# -gt 0 ]]; do
    case "$1" in
    -r | --release)
        BUILD_TYPE="Release"
        shift
        ;;
    -p | --profiling)
        BUILD_TYPE="Profiling"
        shift
        ;;
    -d | --debug)
        BUILD_TYPE="Debug"
        shift
        ;;
    -v | --valgrind)
        BUILD_TYPE="Valgrind"
        shift
        ;;
    -l | --local)
        LOCAL=true
        shift
        ;;
    -u | --user)
        SSH_USER="$2"
        shift 2
        ;;
    *) die "Unknown option: $1" ;;
    esac
done

[[ -n "${BUILD_TYPE}" ]] || die "At least one build type must be specified (-r|-p|-d|-v)."

info "Build type: ${BUILD_TYPE}"
info "Run locally?: ${LOCAL}"
info "SSH user: ${SSH_USER}"

# Prepare log directory
timestamp=$(date +"%Y%m%d_%H%M%S")
log_dir="${BASE_LOG_DIR}/run_${timestamp}"
mkdir -p "${log_dir}"

# Load remote node information
if [[ ! -f "${CONFIG_NETWORK}" ]]; then
    die "Network config not found: ${CONFIG_NETWORK}"
fi

mapfile -t REMOTE_IPS < <(jq -r '.[].ip' "${CONFIG_NETWORK}")

# Helper: start a remote test
start_remote_test() {
    local ssh_user="$1"
    local ip="$2"
    local is_local="$3"
    local build_type="$4"
    local node_id="$5"
    local exec="$6"
    local test_log_dir="$7"
    shift 7
    local params=("$@")

    local bin_dir="${PROJECT_FOLDER}/build/${build_type}/benchmarks"
    local log_file="${test_log_dir}/log_p${node_id}.log"

    # Build the command that will be executed either locally or over SSH
    local prefix=""
    if [[ "${is_local}" == false ]]; then
        prefix="ssh -n -S /tmp/ssh-socket-${ssh_user}@${ip} -o StrictHostKeyChecking=no ${ssh_user}@${ip} '"
    fi

    # Change to the log directory on the remote side
    local cmd="${prefix}mkdir -p ${test_log_dir} && cd ${test_log_dir}"
    if [[ "${build_type}" == "Profiling" ]]; then
        cmd="${cmd} && mkdir ${node_id} && cd ${node_id}"
    fi
    cmd="${cmd} && numactl -m 0 -N 0"
    if [[ "${build_type}" == "Valgrind" ]]; then
        cmd="${cmd} valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes"
    fi
    cmd="${cmd} ${bin_dir}/${exec} ${node_id} ${params[@]}"
    if [[ "${is_local}" == false ]]; then
        cmd="${cmd}'"
    fi
    cmd="${cmd} > ${log_file} 2>&1"

    # Execute the test
    eval "${cmd}"
    local status=$?

    # If profiling, generate the gprof report afterwards
    if [[ "${build_type}" == "Profiling" ]]; then
        local prof_cmd="${prefix} gprof ${bin_dir}/${exec} ${test_log_dir}/${node_id}/gmon.out > ${test_log_dir}/.report_p${node_id}.txt 2>&1; rm ${test_log_dir}/${node_id}/gmon.out; rmdir ${test_log_dir}/${node_id}"
        if [[ "${is_local}" == false ]]; then
            prof_cmd="${prof_cmd}'"
        fi
        prof_cmd="${prof_cmd} &>> ${log_file}"
        if ! eval "${prof_cmd}"; then
            warn "Profiling report generation failed on node ${node_id}"
            warn "Command: ${prof_cmd}"
        fi
    fi

    return $status
}

export -f start_remote_test           # needed for subshells spawned by background jobs
export PROJECT_FOLDER CONFIG_TEST_DIR # make them visible to child processes

# If not local: prepare persistent SSH sockets
if [[ "${LOCAL}" == false ]]; then
    info "Setting up persistent SSH control sockets…"
    for ip in "${REMOTE_IPS[@]}"; do
        if [[ ! -S "/tmp/ssh-socket-${SSH_USER}@${ip}" ]]; then
            echo "$ip"
            # The -M -S options create a master socket; -fN puts it in background
            ssh -o StrictHostKeyChecking=no -M -S "/tmp/ssh-socket-${SSH_USER}@${ip}" "${SSH_USER}@${ip}" -fN ||
                die "Failed to create persistent SSH socket for ${ip}"
        fi
    done
fi

info "Starting tests on ${#REMOTE_IPS[@]} node(s)…"

while read -r line || [[ -n "$line" ]]; do
    echo "$line"
done <"${CONFIG_TESTS}"

while read -r line || [[ -n "$line" ]]; do
    [[ -z "$line" ]] && continue

    IFS=' ' read -r -a parts <<<"$line"
    executable="${parts[0]}"
    params=("${parts[@]:1}")
    config_path="${params[0]}"

    # ---- per-test setup ----
    test_name="$executable"
    test_log_dir="${log_dir}/${test_name}"

    rm -rf "${test_log_dir}"
    mkdir -p "${test_log_dir}"

    info "> Running test: ${test_name}"

    declare -A PIDS=()
    declare -A RETVALS=()

    # Extract nodes for this test
    mapfile -t TEST_IPS < <(jq -r '.[].ip' "${CONFIG_NETWORK}")

    for node_id in "${!TEST_IPS[@]}"; do
        ip="${TEST_IPS[$node_id]}"

        start_remote_test "${SSH_USER}" "${ip}" "${LOCAL}" "${BUILD_TYPE}" "${node_id}" "${executable}" "${test_log_dir}" "${params[@]}" &

        pid=$!
        PIDS[$pid]=$node_id
        RETVALS[$node_id]=0
    done

    # ---- wait for THIS test only ----
    FAILED_NODES=()

    for pid in "${!PIDS[@]}"; do
        if wait "$pid"; then
            exit_code=0
        else
            exit_code=$?
        fi

        node_id="${PIDS[$pid]}"
        RETVALS[$node_id]=$exit_code

        if ((exit_code != 0)); then
            FAILED_NODES+=("$node_id")
            warn "Test ${test_name} failed on node ${node_id}"
        fi
    done

    # ---- summary per test ----
    if ((${#FAILED_NODES[@]} == 0)); then
        echo -e "> ${test_name}: \033[92mPassed\033[0m"
    else
        sorted=$(printf "%s\n" "${FAILED_NODES[@]}" | sort -n | tr '\n' ' ')
        echo -e "> ${test_name}: \033[91mFailed on nodes ${sorted}\033[0m"
    fi

done <"${CONFIG_TESTS}"

# Pull logs back (parallel)
if [[ "${LOCAL}" == false ]]; then
    info "Collecting logs from remote machines (parallel)…"
    for ip in "${REMOTE_IPS[@]}"; do
        rsync -av -e "ssh -o StrictHostKeyChecking=no" \
            "${ip}:${log_dir}/" "${log_dir}/" >/dev/null &
    done
    wait # wait for all rsync jobs to finish
fi

# Close SSH sockets (parallel)
if [[ "${LOCAL}" == false ]]; then
    info "Shutting down persistent SSH sockets (parallel)…"
    for ip in "${REMOTE_IPS[@]}"; do
        ssh -S "/tmp/ssh-socket-${SSH_USER}@${ip}" -O exit "${SSH_USER}@${ip}" \
            >/dev/null 2>&1 || warn "Socket for ${ip} may already be closed" &
    done
    wait # ensure all socket‑close commands have completed

    # Clean up any stray socket files (just in case)
    for ip in "${REMOTE_IPS[@]}"; do
        if [[ -S "/tmp/ssh-socket-${SSH_USER}@${ip}" ]]; then
            while [[ -e "/tmp/ssh-socket-${SSH_USER}@${ip}" ]]; do
                sleep 0.05
            done
        fi
    done
fi

exit 0
