#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
: "${CONDA_PREFIX:?Run conda activate mydict-qt first}"
build="${1:-$root/build/$(uname -s | tr '[:upper:]' '[:lower:]')}"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DBUILD_TESTING=ON
cmake --build "$build" --parallel 4
QT_QPA_PLATFORM=offscreen ctest --test-dir "$build" --output-on-failure
echo "Built: $build"
