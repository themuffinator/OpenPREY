#!/usr/bin/env bash
set -euo pipefail

case "${BASH_SOURCE[0]}" in
    */*) script_dir="${BASH_SOURCE[0]%/*}" ;;
    *) script_dir=. ;;
esac
script_dir="$(CDPATH= cd "${script_dir}" && pwd)"
repo_root="$(CDPATH= cd "${script_dir}/../.." && pwd)"
default_builddir="${repo_root}/builddir"
sync_icons_script="${script_dir}/sync_icons.py"
check_staged_content_script="${script_dir}/check_staged_content_edits.py"
declare -a MESON_CMD=()
PYTHON_CMD=""
declare -a READ_ARRAY_RESULT=()

configure_macos_deployment_target() {
    local host_name=""
    host_name="$(uname -s 2>/dev/null || true)"
    [[ "${host_name}" == "Darwin" ]] || return 0

    if [[ -z "${MACOSX_DEPLOYMENT_TARGET:-}" ]]; then
        # Keep Meson subprojects and companion GameLibs on the same floor as
        # the main project's -mmacosx-version-min setting.
        export MACOSX_DEPLOYMENT_TARGET=11.0
    elif [[ ! "${MACOSX_DEPLOYMENT_TARGET}" =~ ^[0-9]+([.][0-9]+){1,2}$ ]]; then
        echo "MACOSX_DEPLOYMENT_TARGET must be a dotted macOS version, got '${MACOSX_DEPLOYMENT_TARGET}'." >&2
        exit 1
    fi

    echo "macOS deployment target: ${MACOSX_DEPLOYMENT_TARGET}"
}

resolve_meson_cmd() {
    local candidate=""
    local python_cmd=""

    local configured_meson="${OPENPREY_MESON:-${OPENQ4_MESON:-}}"
    if [[ -n "${configured_meson}" ]]; then
        if [[ ! -x "${configured_meson}" ]]; then
            echo "OPENPREY_MESON points to a missing or non-executable Meson: '${configured_meson}'." >&2
            exit 1
        fi

        for candidate in python python3; do
            if python_cmd="$(command -v "${candidate}" 2>/dev/null)"; then
                PYTHON_CMD="${python_cmd}"
                MESON_CMD=("${configured_meson}")
                return
            fi
        done

        echo "Python was not found. Install Python or ensure it is available on PATH." >&2
        exit 1
    fi

    for candidate in python python3; do
        if ! python_cmd="$(command -v "${candidate}" 2>/dev/null)"; then
            continue
        fi

        if [[ -z "${PYTHON_CMD}" ]]; then
            PYTHON_CMD="${python_cmd}"
        fi

        if "${python_cmd}" -c 'import mesonbuild.mesonmain' >/dev/null 2>&1; then
            PYTHON_CMD="${python_cmd}"
            MESON_CMD=("${python_cmd}" -m mesonbuild.mesonmain)
            return
        fi
    done

    if [[ -z "${PYTHON_CMD}" ]]; then
        echo "Python was not found. Install Python or ensure it is available on PATH." >&2
        exit 1
    fi

    if command -v meson >/dev/null 2>&1; then
        MESON_CMD=("$(command -v meson)")
        return
    fi

    echo "Meson was not found. Install it into the active Python environment or make 'meson' available on PATH." >&2
    exit 1
}

run_meson() {
    "${MESON_CMD[@]}" "$@"
}

read_line_array() {
    READ_ARRAY_RESULT=()
    local item=""

    while IFS= read -r item; do
        READ_ARRAY_RESULT+=("${item}")
    done
}

read_nul_array() {
    READ_ARRAY_RESULT=()
    local item=""

    while IFS= read -r -d '' item; do
        READ_ARRAY_RESULT+=("${item}")
    done
}

configure_macos_deployment_target
resolve_meson_cmd

test_meson_build_directory() {
    local build_dir="$1"
    [[ -f "${build_dir}/meson-private/coredata.dat" && -f "${build_dir}/build.ninja" ]]
}

get_compile_build_dir() {
    local build_dir="$default_builddir"
    local has_explicit=0
    local args=("$@")
    local i=0

    while (( i < ${#args[@]} )); do
        local arg="${args[$i]}"
        if [[ "${arg}" == "-C" && $((i + 1)) -lt ${#args[@]} ]]; then
            build_dir="${args[$((i + 1))]}"
            has_explicit=1
            break
        fi

        if [[ "${arg}" == -C* && "${arg}" != "-C" ]]; then
            build_dir="${arg:2}"
            has_explicit=1
            break
        fi

        ((i += 1))
    done

    "${PYTHON_CMD}" - "$build_dir" "$has_explicit" <<'PY'
import os
import sys

print(os.path.abspath(sys.argv[1]))
print(sys.argv[2])
PY
}

get_meson_build_option_value() {
    local build_dir="$1"
    local option_name="$2"
    local intro_options_path="${build_dir}/meson-info/intro-buildoptions.json"
    local cmd_line_path="${build_dir}/meson-private/cmd_line.txt"

    "${PYTHON_CMD}" - "$intro_options_path" "$cmd_line_path" "$option_name" <<'PY'
import configparser
import json
import os
import sys

intro_path, cmd_line_path, option_name = sys.argv[1:4]

if os.path.isfile(intro_path):
    with open(intro_path, "r", encoding="utf-8") as handle:
        options = json.load(handle)
    for option in options:
        if option.get("name") == option_name:
            value = option.get("value")
            if isinstance(value, bool):
                print("true" if value else "false")
            elif value is None:
                print("")
            else:
                print(str(value))
            raise SystemExit(0)

if os.path.isfile(cmd_line_path):
    parser = configparser.RawConfigParser()
    parser.read(cmd_line_path, encoding="utf-8")
    if parser.has_option("options", option_name):
        print(parser.get("options", option_name))
        raise SystemExit(0)

raise SystemExit(1)
PY
}

resolve_gamelibs_repo_path() {
    "${PYTHON_CMD}" - "${repo_root}" "${OPENPREY_GAMELIBS_REPO:-${OPENQ4_GAMELIBS_REPO:-}}" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
raw = sys.argv[2].strip()
repo = pathlib.Path(raw) if raw else root.parent / "OpenPrey-game"
print(repo.resolve().as_posix())
PY
}

test_gamelibs_stage_refresh_needed() {
    local build_dir="$1"
    test_meson_build_directory "${build_dir}" || return 1

    local build_engine=""
    local build_games=""
    build_engine="$(get_meson_build_option_value "${build_dir}" build_engine || true)"
    build_games="$(get_meson_build_option_value "${build_dir}" build_games || true)"
    if [[ "${build_engine}" != "true" && "${build_games}" != "true" ]]; then
        return 1
    fi

    local gamelibs_repo=""
    gamelibs_repo="$(resolve_gamelibs_repo_path)"
    local stage_root="${build_dir}/.tmp/openprey_gamelibs_stage"
    local source_game_dirs=(
        "${gamelibs_repo}/src/game"
        "${gamelibs_repo}/src/Prey"
        "${gamelibs_repo}/src/preyengine"
    )
    local staged_game_dirs=(
        "${stage_root}/src/game"
        "${stage_root}/src/Prey"
        "${stage_root}/src/preyengine"
    )

    local directory_path=""
    for directory_path in "${source_game_dirs[@]}"; do
        [[ -d "${directory_path}" ]] || return 1
    done
    for directory_path in "${staged_game_dirs[@]}"; do
        [[ -d "${directory_path}" ]] || return 0
    done

    if "${PYTHON_CMD}" - "${gamelibs_repo}" "${stage_root}" "${source_game_dirs[@]}" -- "${staged_game_dirs[@]}" <<'PY'
import hashlib
import json
import os
import pathlib
import sys

gamelibs_root = pathlib.Path(sys.argv[1])
stage_root = pathlib.Path(sys.argv[2])
separator = sys.argv.index("--", 3)
source_dirs = sys.argv[3:separator]
staged_dirs = sys.argv[separator + 1:]


def regular_files(directory_paths):
    paths = []
    for directory_path in directory_paths:
        for root, _dirs, files in os.walk(directory_path):
            root_path = pathlib.Path(root)
            paths.extend(root_path / file_name for file_name in files)
    return paths


def latest_file_mtime_ns(paths):
    latest = None
    for path in paths:
        mtime_ns = path.stat().st_mtime_ns
        latest = mtime_ns if latest is None else max(latest, mtime_ns)
    return latest

source_files = regular_files(source_dirs)
staged_files = regular_files(staged_dirs)
source_relative_paths = {path.relative_to(gamelibs_root).as_posix() for path in source_files}
staged_relative_paths = {path.relative_to(stage_root).as_posix() for path in staged_files}
if source_relative_paths != staged_relative_paths:
    raise SystemExit(0)
source_latest = latest_file_mtime_ns(source_files)
staged_latest = latest_file_mtime_ns(staged_files)
if source_latest is None:
    raise SystemExit(1)
if staged_latest is None:
    raise SystemExit(0)
# The normal copy2 path preserves timestamps, so this avoids hashing GameLibs
# on the common no-change path. When a filesystem loses timestamp precision,
# verify potentially newer files against the staging manifest instead of
# treating a timestamp gap as a source edit.
if source_latest <= staged_latest:
    raise SystemExit(1)

try:
    manifest = json.loads(
        (stage_root / "openprey_gamelibs_stage_manifest.json").read_text(encoding="utf-8")
    )
    manifest_hashes = {
        entry["path"]: entry["sha256"]
        for entry in manifest["files"]
        if entry["path"].startswith(("src/game/", "src/Prey/", "src/preyengine/"))
    }
except (KeyError, OSError, TypeError, json.JSONDecodeError):
    raise SystemExit(0)

for source_path in source_files:
    if source_path.stat().st_mtime_ns <= staged_latest:
        continue
    relative_path = source_path.relative_to(gamelibs_root).as_posix()
    expected_hash = manifest_hashes.get(relative_path)
    if expected_hash is None:
        raise SystemExit(0)
    digest = hashlib.sha256()
    with source_path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != expected_hash:
        raise SystemExit(0)
raise SystemExit(1)
PY
    then
        return 0
    else
        return 1
    fi
}

load_build_dir_info() {
    read_line_array < <(get_compile_build_dir "$@")
    BUILD_DIR="${READ_ARRAY_RESULT[0]}"
    BUILD_DIR_HAS_EXPLICIT="${READ_ARRAY_RESULT[1]}"
}

remove_non_runtime_install_artifacts() {
    local install_root="$1"
    [[ -n "${install_root}" && -d "${install_root}" ]] || return 0

    find "${install_root}" -maxdepth 1 -type f \
        \( -name '*.lib' -o -name '*.exp' -o -name '*.ilk' -o -name '*.map' -o -name '*.zip' -o -name 'mgscope_sendinput.cfg' -o -name 'scope_autotest*.cfg' \) \
        -print | while IFS= read -r match; do
            [[ -n "${match}" ]] || continue
            echo "Removing non-runtime staged artifact '${match}'"
            rm -f -- "${match}"
        done

    local install_game_dir="${install_root}/basepr"
    [[ -d "${install_game_dir}" ]] || return 0

    find "${install_game_dir}" -maxdepth 1 -type f \
        \( -name '*.lib' -o -name '*.exp' -o -name '*.ilk' -o -name '*.map' \) \
        -print | while IFS= read -r match; do
            [[ -n "${match}" ]] || continue
            echo "Removing non-runtime staged artifact '${match}'"
            rm -f -- "${match}"
        done
}

declare -a effective_args=()
for arg in "$@"; do
    effective_args+=("${arg%$'\r'}")
done

command_name="${effective_args[0]:-}"
if [[ -z "${command_name}" ]]; then
    echo "No Meson arguments were provided to meson_setup.sh." >&2
    exit 1
fi

if [[ ( "${command_name}" == "setup" || "${command_name}" == "compile" || "${command_name}" == "install" ) && "${OPENPREY_SKIP_ICON_SYNC:-${OPENQ4_SKIP_ICON_SYNC:-0}}" != "1" ]]; then
    if [[ ! -f "${sync_icons_script}" ]]; then
        echo "Icon sync script not found: '${sync_icons_script}'." >&2
        exit 1
    fi

    "${PYTHON_CMD}" "${sync_icons_script}" --source-root "${repo_root}"
fi

if [[ "${command_name}" == "install" ]]; then
    if [[ ! -f "${check_staged_content_script}" ]]; then
        echo "Staged content edit check script not found: '${check_staged_content_script}'." >&2
        exit 1
    fi

    "${PYTHON_CMD}" "${check_staged_content_script}" --source-root "${repo_root}"
fi

if [[ "${command_name}" == "compile" || "${command_name}" == "install" ]]; then
    load_build_dir_info "${effective_args[@]}"

    if [[ "${command_name}" == "compile" ]] && ! test_meson_build_directory "${BUILD_DIR}"; then
        echo "Meson build directory '${BUILD_DIR}' is missing or invalid. Running meson setup..."
        declare -a setup_args=(
            setup
            "${BUILD_DIR}"
            "${repo_root}"
            --backend
            ninja
            --buildtype=debug
            --wrap-mode=forcefallback
        )
        run_meson "${setup_args[@]}"
    fi

    if test_gamelibs_stage_refresh_needed "${BUILD_DIR}"; then
        echo "OpenPrey-game sources changed since the last staged snapshot. Reconfiguring '${BUILD_DIR}'..."
        run_meson setup --reconfigure "${BUILD_DIR}" "${repo_root}"
    fi

    if [[ "${BUILD_DIR_HAS_EXPLICIT}" == "0" ]]; then
        declare -a remaining_args=()
        if (( ${#effective_args[@]} > 1 )); then
            remaining_args=("${effective_args[@]:1}")
        fi
        effective_args=("${effective_args[0]}" -C "${BUILD_DIR}" "${remaining_args[@]}")
    fi

    if [[ "${command_name}" == "install" ]]; then
        found_skip_subprojects=0
        for arg in "${effective_args[@]}"; do
            if [[ "${arg}" == "--skip-subprojects" ]]; then
                found_skip_subprojects=1
                break
            fi
        done
        if [[ "${found_skip_subprojects}" == "0" ]]; then
            effective_args+=(--skip-subprojects)
        fi
    fi
fi

if run_meson "${effective_args[@]}"; then
    exit_code=0
else
    exit_code=$?
fi

if [[ "${exit_code}" == "0" && ( "${command_name}" == "compile" || "${command_name}" == "install" ) ]]; then
    remove_non_runtime_install_artifacts "${repo_root}/.install"
fi

exit "${exit_code}"
