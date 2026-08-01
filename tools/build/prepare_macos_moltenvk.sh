#!/usr/bin/env bash
# Acquire, stage, and validate the pinned MoltenVK runtime for macOS packages.
#
# MoltenVK is a Vulkan-on-Metal translation layer. It is not a native Metal
# renderer, not a Metal renderer, and not a native Vulkan driver. See
# docs/dev/macos-moltenvk-provider-policy.md for the full provider policy.
#
# Usage:
#   tools/build/prepare_macos_moltenvk.sh [--output-dir DIR]
#   tools/build/prepare_macos_moltenvk.sh --verify-only [--output-dir DIR]
#   tools/build/prepare_macos_moltenvk.sh --verify-only --dylib PATH
#   tools/build/prepare_macos_moltenvk.sh --print-sha256
set -euo pipefail

# ---------------------------------------------------------------------------
# PINNED DEPENDENCY CONSTANTS
# Update these together, never individually. See "Updating The Pin" in
# docs/dev/macos-moltenvk-provider-policy.md.
# ---------------------------------------------------------------------------
# MoltenVK v1.4.1 (2025-11-24) is the newest release that still runs on
# macOS 11.0. MoltenVK v1.4.2 raised the runtime floor to macOS 12.0, which is
# above openQ4's macos_deployment_target / MACOS_MIN_SYSTEM_VERSION of 11.0.
# Do NOT "upgrade" this pin to v1.4.2 or later while the project deployment
# target is still 11.0: it would silently drop macOS 11 users.
MOLTENVK_VERSION="v1.4.1"
MOLTENVK_TAR_NAME="MoltenVK-macos.tar"
MOLTENVK_TAR_URL="https://github.com/KhronosGroup/MoltenVK/releases/download/${MOLTENVK_VERSION}/${MOLTENVK_TAR_NAME}"
# Only the plain MoltenVK-macos.tar asset is acceptable. The
# MoltenVK-macos-privateapi.tar variant must never be shipped.
MOLTENVK_TAR_MEMBER="MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"

# Digest of the pinned MoltenVK-macos.tar (v1.4.1), taken from the GitHub
# release API's asset `digest` field, which GitHub computes server-side:
#   GET https://api.github.com/repos/KhronosGroup/MoltenVK/releases/tags/v1.4.1
#   -> assets[MoltenVK-macos.tar].digest, size 56034304 bytes
# It has NOT been confirmed by hashing a local download (this project has no
# macOS host). The first CI run that fetches the tar is that confirmation: the
# download path below verifies this constant and fails closed on a mismatch.
# See "Updating The Pin" in docs/dev/macos-moltenvk-provider-policy.md.
MOLTENVK_TAR_SHA256="5ea0c259df7ded9a275444820f09cced54d6e5a7c7a31d262de62a5cdb7e15cf"
MOLTENVK_TAR_SHA256_PLACEHOLDER="PLACEHOLDER_FILL_IN_MOLTENVK_TAR_SHA256"

# Staged artifact identity and validation floors.
MOLTENVK_DYLIB_NAME="libMoltenVK.dylib"
MOLTENVK_INSTALL_NAME="@executable_path/../Frameworks/libMoltenVK.dylib"
MOLTENVK_EXPECTED_ARCHS="arm64 x86_64"
MOLTENVK_MAX_MINOS="11.0"
# Dependency prefixes permitted in the staged dylib. Anything else (Homebrew,
# MacPorts, /opt, @rpath, or a developer's absolute path) is a ship blocker.
MOLTENVK_ALLOWED_DEP_PREFIXES="/System/Library/ /usr/lib/"

LOG_PREFIX="prepare_macos_moltenvk"

log() {
    printf '%s: %s\n' "${LOG_PREFIX}" "$*"
}

warn() {
    printf '%s: warning: %s\n' "${LOG_PREFIX}" "$*" >&2
}

die() {
    printf '%s: error: %s\n' "${LOG_PREFIX}" "$*" >&2
    exit 1
}

usage() {
    cat <<'USAGE'
Usage: tools/build/prepare_macos_moltenvk.sh [options]

Downloads the pinned MoltenVK release tar, verifies its SHA-256, stages
libMoltenVK.dylib as a real (non-symlink) file, rewrites its install name to
@executable_path/../Frameworks/libMoltenVK.dylib, and validates the result.

Options:
  --output-dir DIR   Directory that receives libMoltenVK.dylib.
                     Default: <repo>/.tmp/moltenvk-macos
  --dylib PATH       Validate this exact dylib (implies the staged path).
                     Useful with --verify-only against an assembled app bundle.
  --cache-dir DIR    Download cache directory. Default: <repo>/.tmp/downloads
  --verify-only      Skip download/staging; re-run every assertion against an
                     already-staged dylib.
  --print-sha256     Download the pinned tar and print its SHA-256, then exit.
                     This is the only mode that works while the checksum
                     constant is still the placeholder. It stages nothing.
  -h, --help         Show this help.

Exit status is non-zero with an actionable message on any failure.
USAGE
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
mode="prepare"
output_dir=""
dylib_override=""
cache_dir=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output-dir)
            [ "$#" -ge 2 ] || die "--output-dir requires a directory argument."
            output_dir="$2"
            shift 2
            ;;
        --output-dir=*)
            output_dir="${1#*=}"
            shift
            ;;
        --dylib)
            [ "$#" -ge 2 ] || die "--dylib requires a file argument."
            dylib_override="$2"
            shift 2
            ;;
        --dylib=*)
            dylib_override="${1#*=}"
            shift
            ;;
        --cache-dir)
            [ "$#" -ge 2 ] || die "--cache-dir requires a directory argument."
            cache_dir="$2"
            shift 2
            ;;
        --cache-dir=*)
            cache_dir="${1#*=}"
            shift
            ;;
        --verify-only)
            mode="verify"
            shift
            ;;
        --print-sha256)
            mode="print-sha256"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "Unknown argument: '$1'"
            ;;
    esac
done

case "$0" in
    */*) script_dir="${0%/*}" ;;
    *) script_dir="." ;;
esac
script_dir="$(CDPATH= cd "${script_dir}" && pwd)"
repo_root="$(CDPATH= cd "${script_dir}/../.." && pwd)"

[ -n "${output_dir}" ] || output_dir="${repo_root}/.tmp/moltenvk-macos"
[ -n "${cache_dir}" ] || cache_dir="${repo_root}/.tmp/downloads"

if [ -n "${dylib_override}" ]; then
    staged_dylib="${dylib_override}"
else
    staged_dylib="${output_dir}/${MOLTENVK_DYLIB_NAME}"
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
require_tool() {
    local tool="$1"
    local why="$2"
    command -v "${tool}" >/dev/null 2>&1 ||
        die "Required tool '${tool}' was not found on PATH (${why}). Install the Xcode Command Line Tools with 'xcode-select --install'."
}

require_macos_host() {
    local host_name=""
    host_name="$(uname -s 2>/dev/null || true)"
    [ "${host_name}" = "Darwin" ] ||
        die "This mode requires a macOS host (lipo/otool/install_name_tool/vtool are macOS tools); detected '${host_name:-unknown}'. Run it on a macos-15 or macos-15-intel runner, or use --print-sha256 which only needs curl and shasum."
}

sha256_of() {
    local path="$1"
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "${path}" | awk '{ print $1 }'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${path}" | awk '{ print $1 }'
    else
        die "Neither 'shasum' nor 'sha256sum' is available, so the MoltenVK download cannot be verified. Refusing to continue unverified."
    fi
}

to_lower() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

is_sha256_literal() {
    case "$1" in
        ''|*[!0-9a-f]*) return 1 ;;
    esac
    [ "${#1}" -eq 64 ]
}

assert_checksum_constant_is_filled_in() {
    if [ "${MOLTENVK_TAR_SHA256}" = "${MOLTENVK_TAR_SHA256_PLACEHOLDER}" ]; then
        die "MOLTENVK_TAR_SHA256 is still the placeholder value '${MOLTENVK_TAR_SHA256_PLACEHOLDER}'.
The pinned MoltenVK tar cannot be verified, so this script refuses to stage anything (fail closed).
To fix it:
  1. Run: tools/build/prepare_macos_moltenvk.sh --print-sha256
  2. Cross-check the printed digest against the ${MOLTENVK_VERSION} release page
     (${MOLTENVK_TAR_URL}) from a second machine or network.
  3. Replace MOLTENVK_TAR_SHA256 near the top of this script with the 64-character
     lowercase hex digest and commit it.
See docs/dev/macos-moltenvk-provider-policy.md, section 'Updating The Pin'."
    fi
    if ! is_sha256_literal "$(to_lower "${MOLTENVK_TAR_SHA256}")"; then
        die "MOLTENVK_TAR_SHA256 is not a 64-character lowercase hex SHA-256 digest: '${MOLTENVK_TAR_SHA256}'.
Fix the constant near the top of this script; see docs/dev/macos-moltenvk-provider-policy.md, section 'Updating The Pin'."
    fi
}

# version_le "11.0" "11.0" -> 0 (true) when the first version is <= the second.
version_le() {
    local lhs="$1"
    local rhs="$2"
    local lparts rparts index lhs_field rhs_field
    local old_ifs="${IFS}"
    IFS='.'
    # shellcheck disable=SC2206 # deliberate '.'-separated field split
    lparts=(${lhs})
    # shellcheck disable=SC2206 # deliberate '.'-separated field split
    rparts=(${rhs})
    IFS="${old_ifs}"

    for index in 0 1 2; do
        lhs_field="${lparts[${index}]:-0}"
        rhs_field="${rparts[${index}]:-0}"
        case "${lhs_field}${rhs_field}" in
            ''|*[!0-9]*) return 2 ;;
        esac
        if [ "${lhs_field}" -lt "${rhs_field}" ]; then
            return 0
        fi
        if [ "${lhs_field}" -gt "${rhs_field}" ]; then
            return 1
        fi
    done
    return 0
}

download_pinned_tar() {
    local destination="$1"
    require_tool curl "downloading the pinned MoltenVK release asset"
    mkdir -p "$(dirname "${destination}")"

    local partial="${destination}.partial"
    rm -f "${partial}"
    log "downloading ${MOLTENVK_TAR_URL}"
    if ! curl -fsSL --retry 5 --retry-delay 3 --retry-connrefused \
        --connect-timeout 30 --max-time 900 \
        -o "${partial}" "${MOLTENVK_TAR_URL}"; then
        rm -f "${partial}"
        die "Failed to download the pinned MoltenVK asset from ${MOLTENVK_TAR_URL}.
Check network access to github.com release downloads on this runner, then re-run.
Do NOT substitute 'brew install molten-vk' or a LunarG SDK copy: those are not acceptable release sources (see docs/dev/macos-moltenvk-provider-policy.md)."
    fi
    mv -f "${partial}" "${destination}"
}

# ---------------------------------------------------------------------------
# Mode: --print-sha256
# ---------------------------------------------------------------------------
if [ "${mode}" = "print-sha256" ]; then
    mkdir -p "${cache_dir}"
    tar_path="${cache_dir}/moltenvk-${MOLTENVK_VERSION}-${MOLTENVK_TAR_NAME}"
    download_pinned_tar "${tar_path}"
    computed="$(to_lower "$(sha256_of "${tar_path}")")"
    log "asset: ${MOLTENVK_TAR_URL}"
    log "local copy: ${tar_path}"
    log "paste this into MOLTENVK_TAR_SHA256 after cross-checking it upstream:"
    printf 'MOLTENVK_TAR_SHA256="%s"\n' "${computed}"
    exit 0
fi

require_macos_host
require_tool lipo "validating that the staged MoltenVK dylib is universal"
require_tool otool "validating MoltenVK install name and dependencies"

# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------
assert_not_symlink() {
    local path="$1"
    if [ -L "${path}" ]; then
        die "Staged MoltenVK dylib is a symlink: '${path}'.
Every downstream packaging and archive-validation stage rejects symlinks. Stage a real file (this script copies with 'cp -L')."
    fi
    if [ ! -f "${path}" ]; then
        die "Staged MoltenVK dylib is missing or is not a regular file: '${path}'.
Run tools/build/prepare_macos_moltenvk.sh (without --verify-only) first, or point --dylib/--output-dir at the staged copy."
    fi
}

assert_universal_archs() {
    local path="$1"
    local raw sorted
    raw="$(lipo -archs "${path}" 2>/dev/null || true)"
    [ -n "${raw}" ] ||
        die "'lipo -archs' produced no output for '${path}'. The file is probably not a Mach-O binary."
    sorted="$(printf '%s' "${raw}" | tr ' \t' '\n\n' | grep -v '^$' | sort | tr '\n' ' ' | sed 's/[[:space:]]*$//')"
    if [ "${sorted}" != "${MOLTENVK_EXPECTED_ARCHS}" ]; then
        die "MoltenVK dylib is not the expected universal binary.
  path:     ${path}
  expected: ${MOLTENVK_EXPECTED_ARCHS} (order-insensitive)
  actual:   ${raw}
A thin or differently-sliced dylib breaks one of the two macOS package variants. Homebrew's molten-vk is arch-thin and must not be used; re-run this script to fetch the official universal release asset."
    fi
    ARCHS_RESULT="${sorted}"
}

assert_install_name() {
    local path="$1"
    local names
    names="$(otool -D "${path}" | awk '!/:[[:space:]]*$/ && NF { print $1 }' | sort -u)"
    [ -n "${names}" ] ||
        die "'otool -D' reported no install name (LC_ID_DYLIB) for '${path}'."
    if [ "${names}" != "${MOLTENVK_INSTALL_NAME}" ]; then
        die "MoltenVK install name is not package-relative.
  path:     ${path}
  expected: ${MOLTENVK_INSTALL_NAME}
  actual:   $(printf '%s' "${names}" | tr '\n' ' ')
Re-run this script without --verify-only so install_name_tool -id can rewrite it, or fix the packaging stage that replaced the dylib."
    fi
}

assert_dependency_allowlist() {
    local path="$1"
    local dependency allowed prefix
    local offenders=""
    while IFS= read -r dependency; do
        [ -n "${dependency}" ] || continue
        if [ "${dependency}" = "${MOLTENVK_INSTALL_NAME}" ]; then
            continue
        fi
        allowed=0
        for prefix in ${MOLTENVK_ALLOWED_DEP_PREFIXES}; do
            case "${dependency}" in
                "${prefix}"*) allowed=1 ;;
            esac
        done
        if [ "${allowed}" -eq 0 ]; then
            offenders="${offenders}  ${dependency}
"
        fi
    done <<EOF
$(otool -L "${path}" | awk '/^[[:space:]]/ { print $1 }' | sort -u)
EOF

    if [ -n "${offenders}" ]; then
        die "MoltenVK dylib links libraries outside the permitted system prefixes.
  path:    ${path}
  allowed: ${MOLTENVK_ALLOWED_DEP_PREFIXES} and its own install name (${MOLTENVK_INSTALL_NAME})
  offending dependencies:
${offenders}A release-safe MoltenVK links only /System/Library/* and /usr/lib/*. Homebrew, MacPorts, /opt, and @rpath dependencies are ship blockers; fetch the official release asset instead of a package-manager build."
    fi
}

assert_minos_floor() {
    local path="$1"
    local values value
    values=""
    if command -v vtool >/dev/null 2>&1; then
        values="$(vtool -show-build "${path}" 2>/dev/null | awk '$1 == "minos" { print $2 }' || true)"
    fi
    if [ -z "${values}" ]; then
        values="$(otool -l "${path}" | awk '
            $1 == "cmd" && ($2 == "LC_BUILD_VERSION" || $2 == "LC_VERSION_MIN_MACOSX") { want = 1; next }
            want && $1 == "minos" { print $2; want = 0; next }
            want && $1 == "version" { print $2; want = 0; next }
            $1 == "cmd" { want = 0 }
        ' || true)"
    fi
    if [ -z "${values}" ]; then
        die "Could not read the macOS minimum-version load command from '${path}' with vtool or otool -l.
Without it the macOS ${MOLTENVK_MAX_MINOS} floor cannot be proven, so this fails closed."
    fi

    MINOS_RESULT=""
    while IFS= read -r value; do
        [ -n "${value}" ] || continue
        case "${value}" in
            ''|*[!0-9.]*)
                die "Unparsable macOS minimum version '${value}' in '${path}'." ;;
        esac
        if ! version_le "${value}" "${MOLTENVK_MAX_MINOS}"; then
            die "MoltenVK dylib requires macOS ${value}, which is above openPREY's ${MOLTENVK_MAX_MINOS} deployment target.
  path: ${path}
This is exactly the MoltenVK v1.4.2 trap: v1.4.2 and later raised the runtime floor to macOS 12.0. Keep MOLTENVK_VERSION pinned to v1.4.1 until openPREY's own macos_deployment_target moves off ${MOLTENVK_MAX_MINOS}."
        fi
        if [ -n "${MINOS_RESULT}" ]; then
            MINOS_RESULT="${MINOS_RESULT},${value}"
        else
            MINOS_RESULT="${value}"
        fi
    done <<EOF
${values}
EOF
}

signature_state() {
    local path="$1"
    if ! command -v codesign >/dev/null 2>&1; then
        printf 'unknown'
        return 0
    fi
    if codesign --verify --strict "${path}" >/dev/null 2>&1; then
        printf 'valid'
    else
        printf 'invalid'
    fi
}

validate_staged_dylib() {
    local path="$1"
    assert_not_symlink "${path}"
    assert_universal_archs "${path}"
    assert_install_name "${path}"
    assert_dependency_allowlist "${path}"
    assert_minos_floor "${path}"
}

print_provenance() {
    local path="$1"
    local dylib_sha signed
    dylib_sha="$(to_lower "$(sha256_of "${path}")")"
    signed="$(signature_state "${path}")"
    printf 'moltenvk_provenance: version=%s tar_sha256=%s dylib_sha256=%s archs="%s" minos=%s install_name=%s signature=%s path=%s\n' \
        "${MOLTENVK_VERSION}" \
        "$(to_lower "${MOLTENVK_TAR_SHA256}")" \
        "${dylib_sha}" \
        "${ARCHS_RESULT}" \
        "${MINOS_RESULT}" \
        "${MOLTENVK_INSTALL_NAME}" \
        "${signed}" \
        "${path}"
}

# ---------------------------------------------------------------------------
# Mode: --verify-only
# ---------------------------------------------------------------------------
if [ "${mode}" = "verify" ]; then
    assert_checksum_constant_is_filled_in
    log "verify-only: ${staged_dylib}"
    validate_staged_dylib "${staged_dylib}"
    log "MoltenVK ${MOLTENVK_VERSION} staged copy passed every assertion."
    print_provenance "${staged_dylib}"
    exit 0
fi

# ---------------------------------------------------------------------------
# Mode: prepare (default)
# ---------------------------------------------------------------------------
assert_checksum_constant_is_filled_in
require_tool tar "extracting the pinned MoltenVK release asset"
require_tool install_name_tool "rewriting the MoltenVK install name"

mkdir -p "${cache_dir}"
mkdir -p "${output_dir}"

tar_path="${cache_dir}/moltenvk-${MOLTENVK_VERSION}-${MOLTENVK_TAR_NAME}"
expected_sha="$(to_lower "${MOLTENVK_TAR_SHA256}")"

if [ -f "${tar_path}" ]; then
    cached_sha="$(to_lower "$(sha256_of "${tar_path}")")"
    if [ "${cached_sha}" = "${expected_sha}" ]; then
        log "reusing verified cached asset: ${tar_path}"
    else
        warn "cached asset checksum mismatch; re-downloading (${tar_path})"
        rm -f "${tar_path}"
    fi
fi

if [ ! -f "${tar_path}" ]; then
    download_pinned_tar "${tar_path}"
fi

actual_sha="$(to_lower "$(sha256_of "${tar_path}")")"
if [ "${actual_sha}" != "${expected_sha}" ]; then
    die "SHA-256 mismatch for the pinned MoltenVK asset. Refusing to stage it.
  url:      ${MOLTENVK_TAR_URL}
  file:     ${tar_path}
  expected: ${expected_sha}
  actual:   ${actual_sha}
Either the download is corrupt or the pin is wrong. Delete '${tar_path}' and re-run.
If MOLTENVK_VERSION was just changed, regenerate the digest with --print-sha256 and update MOLTENVK_TAR_SHA256 (docs/dev/macos-moltenvk-provider-policy.md, 'Updating The Pin'). Never relax this check."
fi
log "verified ${MOLTENVK_TAR_NAME} sha256=${actual_sha}"

extract_dir="$(mktemp -d "${TMPDIR:-/tmp}/openq4-moltenvk.XXXXXX")"
cleanup_extract_dir() {
    [ -n "${extract_dir:-}" ] || return 0
    case "${extract_dir}" in
        /|""|/.) return 0 ;;
    esac
    rm -rf "${extract_dir}"
}
trap cleanup_extract_dir EXIT

if ! tar -xf "${tar_path}" -C "${extract_dir}" "${MOLTENVK_TAR_MEMBER}" 2>/dev/null; then
    die "The pinned MoltenVK tar does not contain '${MOLTENVK_TAR_MEMBER}'.
  file: ${tar_path}
Upstream may have changed the archive layout. Inspect it with 'tar -tf \"${tar_path}\"' and update MOLTENVK_TAR_MEMBER together with the version pin."
fi

extracted="${extract_dir}/${MOLTENVK_TAR_MEMBER}"
[ -e "${extracted}" ] ||
    die "Extraction reported success but '${extracted}' does not exist. Delete '${tar_path}' and re-run."

# The staged artifact must be a real file: downstream packaging, archive
# validation, and notarization stages all reject symlinks.
rm -f "${staged_dylib}"
cp -L "${extracted}" "${staged_dylib}" ||
    die "Failed to copy the extracted MoltenVK dylib to '${staged_dylib}'."
chmod u+w "${staged_dylib}"

if [ -L "${staged_dylib}" ]; then
    die "'${staged_dylib}' is still a symlink after 'cp -L'. Refusing to continue."
fi

install_name_tool -id "${MOLTENVK_INSTALL_NAME}" "${staged_dylib}" ||
    die "install_name_tool failed to set the install name on '${staged_dylib}'."

# install_name_tool invalidates the upstream ad-hoc signature. Re-apply an
# ad-hoc signature so the staged dylib stays loadable on Apple silicon. Release
# packaging replaces this with a Developer ID signature, signed inside-out
# before the app bundle is signed.
if command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "${staged_dylib}" >/dev/null 2>&1 ||
        die "Ad-hoc codesign failed for '${staged_dylib}'. install_name_tool invalidated the upstream ad-hoc signature, so the dylib would not load on Apple silicon."
else
    warn "codesign is unavailable; the staged dylib is left unsigned and will not load on Apple silicon until it is signed."
fi

validate_staged_dylib "${staged_dylib}"

if [ "$(signature_state "${staged_dylib}")" != "valid" ]; then
    die "'${staged_dylib}' does not carry a valid code signature after staging.
Release packaging must re-sign it with the Developer ID identity before the app bundle is signed."
fi

log "staged MoltenVK ${MOLTENVK_VERSION} to ${staged_dylib}"
log "MoltenVK is a Vulkan-on-Metal translation layer. It is not a native Metal or native Vulkan renderer."
print_provenance "${staged_dylib}"
