#!/bin/bash

# Default values
SSH_USER="ubuntu"

# Help
print_help() {
    cat <<EOF
Usage: ${0##*/} [options]

Options:
  -h          Show this help message and exit
  -u <user>   ssh user (default: ubuntu)

Example:
  ${0##*/} -u ubuntu
EOF
}

# Parse command line options
while getopts ":hu:" opt; do
    case "$opt" in
    h)
        print_help
        exit 0
        ;;
    u)
        SSH_USER="$OPTARG"
        ;;
    :) # Missing argument
        echo "Error: Option -$OPTARG requires an argument." >&2
        exit 1
        ;;
    \?) # Unknown option
        echo "Error: Invalid option -$OPTARG" >&2
        exit 1
        ;;
    esac
done

# Shift away the processed options so $@ contains only positional args
shift $((OPTIND - 1))

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/../"

project_name="$(basename $PWD)"

config=$(jq '.' "./config.json")
node_ids=($(echo $config | jq -r '.[] | "\(.id)"'))
node_ips=($(echo $config | jq -r '.[] | "\(.ip)"'))

# Close the shared sockets
for ((i = 1; i < ${#node_ips[@]}; i++)); do # put i=0 to kill local node too
    id=${node_ids[i]}
    ip=${node_ips[i]}

    echo "Cleanup of node$id ($ip)..."
    ssh -S /tmp/ssh-socket-$SSH_USER@$ip -O exit $SSH_USER@$ip
    ssh -o StrictHostKeyChecking=no "$SSH_USER@$ip" "pkill -u $SSH_USER -TERM && echo '✅ killed' || echo '⚪ not found'"
done
