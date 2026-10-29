#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_checkout=${1:-"$task_root/_deps/dynarmic"}
task_revision=a41c380246d3d9f9874f0f792d234dc0cc17c180
if [ -e "$task_checkout" ]; then
    echo "Destination already exists; refusing to reset or patch it twice." >&2
    exit 1
fi
git clone https://github.com/lioncash/dynarmic.git "$task_checkout"
git -C "$task_checkout" checkout --detach "$task_revision"
git -C "$task_checkout" apply --check "$task_root/patches/dynarmic-libnx-jit.patch"
git -C "$task_checkout" apply "$task_root/patches/dynarmic-libnx-jit.patch"
echo "Pinned and patched: $task_checkout"
