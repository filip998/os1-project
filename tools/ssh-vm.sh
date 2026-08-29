#!/bin/sh
set -eu

key="${OS1_VM_SSH_KEY:-$HOME/.ssh/os1_vm_ed25519}"
port="${OS1_VM_SSH_PORT:-2222}"
user="${OS1_VM_USER:-os}"

exec ssh \
    -i "$key" \
    -p "$port" \
    -o BatchMode=yes \
    -o LogLevel=ERROR \
    "$user@127.0.0.1" \
    "$@"
