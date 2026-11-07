#!/usr/bin/env bash

set -e

diff -u <(awk < lix/libstore/platform/linux.cc '/BEGIN extract-syscalls/ { extracting = 1; next }
match($0, /allowSyscall\(ctx, SCMP_(ARM_)?SYS\(([^)]*)\)\);|\/\/ skip ([^ ]*)/, result) { print result[2] result[3] }
/END extract-syscalls/ { extracting = 0; next }') <(tail -n+2 "$1" | cut -d, -f 1)
