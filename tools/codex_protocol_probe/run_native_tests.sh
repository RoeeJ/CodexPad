#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cjson_dir=${CJSON_DIR:-${IDF_PATH:-/tmp/esp-idf-5.3.1}/components/json/cJSON}
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

cc -std=c11 -Wall -Wextra -Werror \
    -I"$repo_root/firmware/dpp_fw/main" \
    -I"$cjson_dir" \
    "$repo_root/firmware/dpp_fw/main/codex_rpc.c" \
    "$cjson_dir/cJSON.c" \
    "$repo_root/tools/codex_protocol_probe/test_codex_rpc.c" \
    -lm \
    -o "$build_dir/codex-rpc-test"
"$build_dir/codex-rpc-test"

printf '%s\n' "native Codex controller tests passed"
