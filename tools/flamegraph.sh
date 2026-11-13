#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [ "${OS:-}" = "Windows_NT" ]; then
    exec wsl.exe -d "${LUX_WSL_DISTRO:-Ubuntu}" --cd "$(pwd -W)" bash tools/flamegraph.sh "$@"
fi

flamegraph_rev=41fee1f99f9276008b7cd112fca19dc3ea84ac32
out=build/flame
fg=build/FlameGraph

if perf --version > /dev/null 2>&1; then
    perf=perf
else
    perf=$(ls -d /usr/lib/linux-tools/*/perf 2> /dev/null | sort -V | tail -n 1 || true)
fi
if [ -z "$perf" ]; then
    echo "perf not found. Install it with: sudo apt-get install linux-tools-generic" >&2
    exit 1
fi

mkdir -p "$out"

if [ ! -d "$fg/.git" ]; then
    git clone https://github.com/brendangregg/FlameGraph "$fg"
fi
git -C "$fg" checkout --quiet "$flamegraph_rev"

make -j"$(nproc)" MODE=release BUILD="$out/obj" \
    EXTRA_CXXFLAGS="-g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer" \
    "$out/obj/luxbench"

args=("$@")
[ ${#args[@]} -eq 0 ] && args=(--fuzzy 1 --passes 5 --removes 0)

data=$(mktemp -d)
trap 'rm -rf "$data"' EXIT

"$perf" record -e cpu-clock:u -F 999 -g --user-callchains -o "$data/perf.data" -- "$out/obj/luxbench" "${args[@]}"

"$perf" script -i "$data/perf.data" |"$fg/stackcollapse-perf.pl" > "$out/luxbench.folded"
"$fg/flamegraph.pl" --countname ns --title "luxbench ${args[*]}" "$out/luxbench.folded" > "$out/luxbench.svg"

echo "$PWD/$out/luxbench.svg"
if command -v wslpath > /dev/null; then
    wslpath -w "$PWD/$out/luxbench.svg"
fi
