#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# Constants
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MAIN_CONFIG="${PROJECT_ROOT}/config.json"
CONFIG_SETUP_SCRIPT="${PROJECT_ROOT}/scripts/tests/setup.py"
LOGS_BUILD_DIR="${PROJECT_ROOT}/build/logs_build"

# Build‑type identifiers
REL="Release"
DBG="Debug"
PROF="Profiling"
VG="Valgrind"

# Helpers
die() {
    echo "❌ $*" >&2
    exit 1
}
info() { echo "ℹ️ $*"; }
success() { echo "✅ $*"; }
error() { echo "❌ $*" >&2; }

run_cmd() {
    # Run a command, stream stdout, capture stderr for reporting
    local cmd="$1"
    eval "$cmd" 2> >(while read -r line; do echo "Error: $line"; done)
}

# -------------------- argument parsing ----------------------
# Using getopts for short options; long option `--uselocal` handled manually
BUILD_TYPES=()
CLEAN_BUILD=false
LOCAL_BUILD=false
BUILD_TESTS=false
USE_LOCAL=false

while [[ $# -gt 0 ]]; do
    case "$1" in
    -r | --release)
        BUILD_TYPES+=("$REL")
        shift
        ;;
    -p | --profiling)
        BUILD_TYPES+=("$PROF")
        shift
        ;;
    -d | --debug)
        BUILD_TYPES+=("$DBG")
        shift
        ;;
    -v | --valgrind)
        BUILD_TYPES+=("$VG")
        shift
        ;;
    -c | --clean)
        CLEAN_BUILD=true
        shift
        ;;
    -l | --local)
        LOCAL_BUILD=true
        shift
        ;;
    -t | --tests)
        BUILD_TESTS=True
        shift
        ;; # True (capital T for CMake)
    --uselocal)
        USE_LOCAL=true
        shift
        ;;
    *) die "Unknown option: $1" ;;
    esac
done

[[ ${#BUILD_TYPES[@]} -gt 0 ]] || die "At least one build type must be specified (-r, -p, -d, -v)."

# Preparation
cd "$PROJECT_ROOT"

# Create top‑level build folder
mkdir -p build

# Run the Python config generator
PY_CMD="python3 \"$CONFIG_SETUP_SCRIPT\""
[[ $LOCAL_BUILD == true ]] && PY_CMD+=" -l"
[[ $USE_LOCAL == true ]] && PY_CMD+=" --uselocal"

info "Generating benchmark configuration files..."
if ! output=$(eval "$PY_CMD" 2>&1); then
    die "Config generation failed: $output"
else
    echo "$output"
fi

# Build loop
for BUILD in "${BUILD_TYPES[@]}"; do
    BUILD_PATH="build/${BUILD}"
    if $CLEAN_BUILD || [[ ! -d "$BUILD_PATH" ]]; then
        info "Initialising $BUILD ..."
        rm -rf "$BUILD_PATH" 2>/dev/null || true
        mkdir -p "$BUILD_PATH"
    fi

    info "Building $BUILD ..."
    pushd "$BUILD_PATH" >/dev/null

    # Determine CMake variables
    case "$BUILD" in
    "$PROF")
        CMAKE_BUILD_TYPE="$REL"
        CXX_FLAGS="-g -pg"
        ;;
    "$DBG")
        CMAKE_BUILD_TYPE="$DBG"
        CXX_FLAGS="-fsanitize=address,undefined -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g"
        ;;
    "$VG")
        CMAKE_BUILD_TYPE="$DBG"
        CXX_FLAGS="-g -O0"
        ;;
    *)
        CMAKE_BUILD_TYPE="$BUILD"
        CXX_FLAGS=""
        ;;
    esac

    # Run cmake configuration
    CMAKE_CMD="cmake ../.. \
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -DCMAKE_CXX_FLAGS=\"${CXX_FLAGS}\" \
        -DPROFILING=${BUILD_TYPES[*]/$PROF/true} \
        -DBUILD_TESTS=${BUILD_TESTS}"
    run_cmd "$CMAKE_CMD"

    # Build the target
    run_cmd "cmake --build ."

    popd >/dev/null
    success "Build process of $BUILD complete"
done

# Remote upload
if ! $LOCAL_BUILD; then
    info "Preparing remote upload …"

    # Clean/create the remote‑logs folder
    rm -rf "$LOGS_BUILD_DIR" 2>/dev/null || true
    mkdir -p "$LOGS_BUILD_DIR"

    # Extract hostnames and IDs from config.json (skip the first entry – assumed to be the local node)
    mapfile -t HOSTNAMES < <(jq -r 'to_entries[] | select(.key != 0) | .value.hostname' "$MAIN_CONFIG")
    mapfile -t IDS < <(jq -r 'to_entries[] | select(.key != 0) | .value.id' "$MAIN_CONFIG")

    PROJECT_NAME="$(basename "$(pwd)")"

    for i in "${!HOSTNAMES[@]}"; do
        H="${HOSTNAMES[$i]}"
        ID="${IDS[$i]}"
        LOGFILE="${LOGS_BUILD_DIR}/log_build_node${ID}.log"

        info "Uploading to node${ID} (${H}) …"

        rsync -av --delete \
            --exclude="build/logs_build/***" \
            --include="build/***" \
            --include="benchmarks" \
            --include="benchmarks/config" \
            --include="benchmarks/config/***" \
            --include="benchmarks/logs" \
            --exclude="*" \
            -e "ssh -o StrictHostKeyChecking=no" \
            --rsync-path="mkdir -p ~/${PROJECT_NAME}/ && rsync" \
            "./" "${H}:~/${PROJECT_NAME}/" \
            >"$LOGFILE" 2>&1 &

        JOBS["$ID"]=$!
    done

    info "Waiting for all uploads to finish …"

    # Wait for uploads and report
    for ID in "${!JOBS[@]}"; do
        PID="${JOBS[$ID]}"
        LOGFILE="${LOGS_BUILD_DIR}/log_build_node${ID}.log"

        if wait "$PID"; then
            success "Uploaded to node${ID} (log: $LOGFILE)"
        else
            error "Upload to node${ID} FAILED (see $LOGFILE)"
        fi
    done
else
    info "Local-only build requested - skipping remote upload."
fi

info "All done!"
