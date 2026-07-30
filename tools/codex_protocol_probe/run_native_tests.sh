#!/bin/sh
set -eu

repo_root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${TMPDIR:-/tmp}/codexpad-native-tests

mkdir -p "$build_dir"

cc -std=c11 -Wall -Wextra -Werror \
    -DCODEX_SETTINGS_NO_NVS \
    -I"$repo_root/firmware/dpp_fw/main" \
    "$repo_root/firmware/dpp_fw/main/codex_wire.c" \
    "$repo_root/firmware/dpp_fw/main/codex_controls.c" \
    "$repo_root/firmware/dpp_fw/main/codex_lighting.c" \
    "$repo_root/firmware/dpp_fw/main/codex_settings.c" \
    "$repo_root/tools/codex_protocol_probe/test_codex_wire.c" \
    -o "$build_dir/codex-core-test"
"$build_dir/codex-core-test"

if [ -n "${CJSON_DIR:-}" ]; then
    cjson_dir=$CJSON_DIR
elif [ -n "${IDF_PATH:-}" ]; then
    cjson_dir=$IDF_PATH/components/json/cJSON
else
    cjson_dir=
fi

if [ -n "$cjson_dir" ] && [ -f "$cjson_dir/cJSON.c" ] &&
   [ -f "$cjson_dir/cJSON.h" ]; then
    cc -std=c11 -Wall -Wextra -Werror \
        -I"$repo_root/firmware/dpp_fw/main" \
        -I"$cjson_dir" \
        "$repo_root/firmware/dpp_fw/main/codex_rpc.c" \
        "$cjson_dir/cJSON.c" \
        "$repo_root/tools/codex_protocol_probe/test_codex_rpc.c" \
        -lm \
        -o "$build_dir/codex-rpc-test"
elif command -v pkg-config >/dev/null 2>&1 &&
     pkg-config --exists libcjson; then
    # pkg-config output is intentionally split into compiler arguments.
    # shellcheck disable=SC2046
    cc -std=c11 -Wall -Wextra -Werror \
        -I"$repo_root/firmware/dpp_fw/main" \
        $(pkg-config --cflags libcjson) \
        "$repo_root/firmware/dpp_fw/main/codex_rpc.c" \
        "$repo_root/tools/codex_protocol_probe/test_codex_rpc.c" \
        $(pkg-config --libs libcjson) -lm \
        -o "$build_dir/codex-rpc-test"
else
    printf '%s\n' \
        "cJSON development sources not found." \
        "Set IDF_PATH or CJSON_DIR, or install the libcjson development package." \
        >&2
    exit 1
fi
"$build_dir/codex-rpc-test"

printf '%s\n' "native Codex controller tests passed"
