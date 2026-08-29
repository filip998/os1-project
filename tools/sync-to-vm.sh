#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(dirname "$script_dir")

key="${OS1_VM_SSH_KEY:-$HOME/.ssh/os1_vm_ed25519}"
port="${OS1_VM_SSH_PORT:-2222}"
user="${OS1_VM_USER:-os}"
remote_dir="${OS1_VM_PROJECT_DIR:-/home/os/os1-project}"

rsync -az --delete \
    --exclude '.git/' \
    --exclude 'build/' \
    --exclude 'kernel' \
    --exclude 'kernel.asm' \
    --exclude '.gdbinit' \
    --exclude 'debug-artifacts/' \
    -e "ssh -i $key -p $port -o LogLevel=ERROR" \
    "$project_dir/" \
    "$user@127.0.0.1:$remote_dir/"
