#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Install base deps (local machine only)
sudo apt-get update -qq
sudo apt-get install -y python3 jq

# Read internal dependencies
PKGS=()
while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line%"${line##*[![:space:]]}"}"
    [[ -z "$line" || "$line" == \#* ]] && continue
    PKGS+=("$line")
done < "$script_dir/../requirements.txt"

if [[ ${#PKGS[@]} -eq 0 ]]; then
    echo "⚪ No extra packages to install"
    exit 0
fi

echo "Installing ${#PKGS[@]} packages: ${PKGS[*]}"
sudo apt-get install -y "${PKGS[@]}"

echo "✅ Installation complete"
