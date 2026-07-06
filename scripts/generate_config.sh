#!/usr/bin/env bash
set -euo pipefail

CL_MANIFEST=""
INPUT_CONFIG_PATH=""

print_help() {
    cat <<EOF
Usage: ${0##*/} [options]

Options:
  -h          Show this help message and exit
  -c <file>   Generate the CloudLab config given the manifest
  -l <file>   Use the given local configuration file instead of generating it
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

while getopts ":hc:l:" opt; do
    case "$opt" in
        h) print_help; exit 0 ;;
        c)
            [[ -n $INPUT_CONFIG_PATH ]] && { echo "-c and -l are mutually exclusive" >&2; exit 1; }
            [[ -f "$OPTARG" ]] || { echo "Invalid manifest: $OPTARG" >&2; exit 1; }
            CL_MANIFEST="$OPTARG"
            ;;
        l)
            [[ -n $CL_MANIFEST ]] && { echo "-c and -l are mutually exclusive" >&2; exit 1; }
            [[ -f "$OPTARG" ]] || { echo "Invalid config: $OPTARG" >&2; exit 1; }
            INPUT_CONFIG_PATH="$OPTARG"
            ;;
        :) echo "Missing argument for -$OPTARG" >&2; exit 1 ;;
        \?) echo "Invalid option -$OPTARG" >&2; exit 1 ;;
    esac
done

shift $((OPTIND - 1))

# Generate config
if [[ -n "$CL_MANIFEST" ]]; then
    python3 "$script_dir/setup/cl_config.py" -c "$CL_MANIFEST"
elif [[ -z "$INPUT_CONFIG_PATH" ]]; then
    python3 "$script_dir/setup/local_config.py"
else
    cp "$INPUT_CONFIG_PATH" "$script_dir/../config.json"
fi

echo "✅ Config generated at $script_dir/../config.json"
