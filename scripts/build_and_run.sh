#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

cmake_args=(-S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON)
if [[ -n "${GODOT_CPP_DIR:-}" ]]; then
	cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_GODOT_CPP=${GODOT_CPP_DIR}")
fi

cmake "${cmake_args[@]}"
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path "$root"
