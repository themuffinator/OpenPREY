#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../.." && pwd)"

install_root="${OPENPREY_INSTALL_ROOT:-${OPENQ4_INSTALL_ROOT:-${repo_root}/.install}}"
desktop_dir="${OPENPREY_DESKTOP_DIR:-${OPENQ4_DESKTOP_DIR:-}}"
launcher_name="${OPENPREY_DESKTOP_LAUNCHER_NAME:-${OPENQ4_DESKTOP_LAUNCHER_NAME:-openPREY.desktop}}"
client_binary="${OPENPREY_CLIENT_BINARY:-${OPENQ4_CLIENT_BINARY:-}}"
basepath="${OPENPREY_BASEPATH:-${OPENQ4_BASEPATH:-}}"

usage() {
    cat <<'EOF'
Usage: install_desktop_launcher.sh [options]

Creates an openPREY launcher on the current Linux user's desktop.

Options:
  --install-root PATH   Staged openPREY runtime root. Defaults to repo .install.
  --desktop-dir PATH    Desktop directory. Defaults to xdg-user-dir DESKTOP.
  --name FILENAME       Launcher filename. Defaults to openPREY.desktop.
  --client PATH         Client binary. Defaults to openPREY-client_<host arch>.
  --basepath PATH       Prey (2006) install root containing base/.
  --no-basepath         Do not write fs_basepath into the launcher.
  -h, --help            Show this help.
EOF
}

while (($# > 0)); do
    case "$1" in
        --install-root)
            if (($# < 2)); then
                echo "Missing value for --install-root." >&2
                exit 2
            fi
            install_root="$2"
            shift 2
            ;;
        --desktop-dir)
            if (($# < 2)); then
                echo "Missing value for --desktop-dir." >&2
                exit 2
            fi
            desktop_dir="$2"
            shift 2
            ;;
        --name)
            if (($# < 2)); then
                echo "Missing value for --name." >&2
                exit 2
            fi
            launcher_name="$2"
            shift 2
            ;;
        --client)
            if (($# < 2)); then
                echo "Missing value for --client." >&2
                exit 2
            fi
            client_binary="$2"
            shift 2
            ;;
        --basepath)
            if (($# < 2)); then
                echo "Missing value for --basepath." >&2
                exit 2
            fi
            basepath="$2"
            shift 2
            ;;
        --no-basepath)
            basepath=""
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

canonicalize_existing_dir() {
    local path="$1"
    if [[ ! -d "${path}" ]]; then
        echo "Directory was not found: ${path}" >&2
        exit 1
    fi
    (CDPATH= cd -- "${path}" && pwd)
}

canonicalize_existing_file() {
    local path="$1"
    if [[ ! -f "${path}" ]]; then
        echo "File was not found: ${path}" >&2
        exit 1
    fi
    local dir
    dir="$(CDPATH= cd -- "$(dirname -- "${path}")" && pwd)"
    printf '%s/%s\n' "${dir}" "$(basename -- "${path}")"
}

desktop_quote() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//\"/\\\"}"
    value="${value//%/%%}"
    printf '"%s"' "${value}"
}

host_arch_suffix() {
    case "$(uname -m)" in
        x86_64|amd64)
            printf 'x64\n'
            ;;
        i386|i486|i586|i686)
            printf 'x86\n'
            ;;
        aarch64|arm64)
            printf 'arm64\n'
            ;;
        *)
            printf '%s\n' "$(uname -m)"
            ;;
    esac
}

find_client_binary() {
    if [[ -n "${client_binary}" ]]; then
        client_binary="$(canonicalize_existing_file "${client_binary}")"
        if [[ ! -x "${client_binary}" ]]; then
            echo "openPREY client is not executable: ${client_binary}" >&2
            exit 1
        fi
        printf '%s\n' "${client_binary}"
        return
    fi

    local arch
    arch="$(host_arch_suffix)"
    local preferred="${install_root}/openPREY-client_${arch}"
    if [[ -x "${preferred}" ]]; then
        canonicalize_existing_file "${preferred}"
        return
    fi

    local candidate=""
    while IFS= read -r candidate; do
        if [[ -x "${candidate}" ]]; then
            canonicalize_existing_file "${candidate}"
            return
        fi
    done < <(find "${install_root}" -maxdepth 1 -type f -name 'openPREY-client_*' | sort)

    echo "No executable openPREY client was found under ${install_root}." >&2
    echo "Run the Linux install step first, then retry this launcher install." >&2
    exit 1
}

find_icon() {
    local candidate=""
    for candidate in \
        "${install_root}/share/icons/hicolor/scalable/apps/openprey.svg" \
        "${install_root}/share/icons/hicolor/256x256/apps/openprey.png" \
        "${install_root}/share/icons/hicolor/128x128/apps/openprey.png" \
        "${repo_root}/assets/icons/prey.svg" \
        "${repo_root}/assets/icons/prey_256.png"; do
        if [[ -f "${candidate}" ]]; then
            canonicalize_existing_file "${candidate}"
            return
        fi
    done

    printf 'openprey\n'
}

resolve_desktop_dir() {
    if [[ -n "${desktop_dir}" ]]; then
        mkdir -p -- "${desktop_dir}"
        canonicalize_existing_dir "${desktop_dir}"
        return
    fi

    if command -v xdg-user-dir >/dev/null 2>&1; then
        desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    fi

    if [[ -z "${desktop_dir}" || "${desktop_dir}" == "${HOME}" ]]; then
        desktop_dir="${HOME}/Desktop"
    fi

    mkdir -p -- "${desktop_dir}"
    canonicalize_existing_dir "${desktop_dir}"
}

install_root="$(canonicalize_existing_dir "${install_root}")"
client_binary="$(find_client_binary)"
icon_path="$(find_icon)"
desktop_dir="$(resolve_desktop_dir)"

if [[ "${launcher_name}" == */* || "${launcher_name}" == *\\* ]]; then
    echo "Launcher name must be a filename, not a path: ${launcher_name}" >&2
    exit 2
fi
if [[ "${launcher_name}" != *.desktop ]]; then
    launcher_name="${launcher_name}.desktop"
fi

if [[ -n "${basepath}" ]]; then
    basepath="$(canonicalize_existing_dir "${basepath}")"
    if [[ ! -d "${basepath}/base" ]]; then
        echo "Prey basepath does not contain base/: ${basepath}" >&2
        exit 1
    fi
fi

launcher_path="${desktop_dir}/${launcher_name}"
exec_line="$(desktop_quote "${client_binary}")"
if [[ -n "${basepath}" ]]; then
    exec_line+=" +set fs_basepath $(desktop_quote "${basepath}")"
fi

{
    printf '%s\n' '[Desktop Entry]'
    printf '%s\n' 'Version=1.0'
    printf '%s\n' 'Type=Application'
    printf '%s\n' 'Name=openPREY'
    printf '%s\n' 'GenericName=First-person shooter'
    printf '%s\n' 'Comment=Modern open-source engine and game-code replacement for Prey (2006)'
    printf 'Exec=%s\n' "${exec_line}"
    printf 'Path=%s\n' "${install_root}"
    printf 'Icon=%s\n' "${icon_path}"
    printf '%s\n' 'Terminal=false'
    printf '%s\n' 'Categories=Game;ActionGame;Shooter;'
    printf '%s\n' 'Keywords=prey;idtech;fps;multiplayer;'
    printf '%s\n' 'StartupNotify=true'
} > "${launcher_path}"

chmod 755 "${launcher_path}"

if command -v gio >/dev/null 2>&1; then
    gio set "${launcher_path}" metadata::trusted true >/dev/null 2>&1 || true
fi

echo "Installed openPREY desktop launcher: ${launcher_path}"
