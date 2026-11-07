#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

ext=""
[ "${OS:-}" = "Windows_NT" ] && ext=".exe"
exe="build/release/luxexample$ext"

rm -rf build/release

start=$(date +%s%N)
make MODE=release lib "$exe" "$@" > /dev/null
end=$(date +%s%N)

elapsed_ms=$(( (end - start) / 1000000 ))
lib_bytes=$(wc -c < build/release/libluxcore.a)
exe_bytes=$(wc -c < "$exe")

report=$(printf 'library and example build time: %d.%03d s\nlibluxcore.a: %d bytes\nexample executable: %d bytes\n' \
    $((elapsed_ms / 1000)) $((elapsed_ms % 1000)) "$lib_bytes" "$exe_bytes")

echo "$report"

if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo '### Library size and build time'
        echo '```'
        echo "$report"
        echo '```'
    } >> "$GITHUB_STEP_SUMMARY"
fi
