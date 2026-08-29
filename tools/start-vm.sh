#!/bin/sh
set -eu

project_dir="${OS1_PROJECT_DIR:-$HOME/code/os1-project}"
disk="${OS1_VM_DISK:-$HOME/Virtual Machines.localized/OS_student-import/OS_student.qcow2}"
ssh_port="${OS1_VM_SSH_PORT:-2222}"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "qemu-system-x86_64 is not installed" >&2
    exit 1
fi

if [ ! -d "$project_dir" ]; then
    echo "Project directory does not exist: $project_dir" >&2
    exit 1
fi

if [ ! -f "$disk" ]; then
    echo "VM disk does not exist: $disk" >&2
    exit 1
fi

if lsof -nP -iTCP:"$ssh_port" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "Host TCP port $ssh_port is already in use" >&2
    exit 1
fi

exec qemu-system-x86_64 \
    -name OS_student \
    -machine pc \
    -accel tcg,thread=multi \
    -cpu max \
    -smp 2 \
    -m 4096 \
    -boot c \
    -drive file="$disk",format=qcow2,if=ide \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:"$ssh_port"-:22 \
    -device e1000,netdev=net0 \
    -fsdev local,id=os1share,path="$project_dir",security_model=mapped-xattr \
    -device virtio-9p-pci,fsdev=os1share,mount_tag=os1share \
    -device qemu-xhci \
    -device usb-kbd \
    -device usb-tablet \
    -display cocoa,show-cursor=on
