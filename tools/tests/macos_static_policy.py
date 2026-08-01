#!/usr/bin/env python3
"""Static policy checks for macOS platform-risk boundaries."""

from pathlib import Path
import importlib.util
import re


ROOT = Path(__file__).resolve().parents[2]


def load_meson_sources_module():
    module_path = ROOT / "tools" / "build" / "meson_sources.py"
    spec = importlib.util.spec_from_file_location("openq4_meson_sources", module_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Could not load Meson source manifest module: {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def derive_active_macos_sources() -> tuple[str, ...]:
    module = load_meson_sources_module()
    sources = tuple(
        sorted(
            f"src/{relative_path}"
            for relative_path in set(module.SDL3_DARWIN_SOURCES) | set(module.DARWIN_PLATFORM_SOURCES)
        )
    )
    if not sources:
        raise AssertionError("Derived active macOS source set is empty")
    return sources


ACTIVE_MACOS_SOURCES = derive_active_macos_sources()

UNSAFE_PROCESS_PATTERNS = {
    "execvp": re.compile(r"\bexecvp\s*\("),
    "posix_spawnp": re.compile(r"\bposix_spawnp\s*\("),
    "system": re.compile(r"\bsystem\s*\("),
    "popen": re.compile(r"\bpopen\s*\("),
}


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def assignment_block(source: str, name: str) -> str:
    start = source.find(f"{name} = (")
    if start == -1:
        raise AssertionError(f"Missing assignment block {name!r}")
    end = source.find("\n)", start)
    if end == -1:
        raise AssertionError(f"Could not find end of assignment block {name!r}")
    return source[start : end + 2]


def files_containing(token: str) -> list[str]:
    return sorted(path for path in ACTIVE_MACOS_SOURCES if token in read(path))


def strip_c_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return "\n".join(line.split("//", 1)[0] for line in source.splitlines())


def validate_process_launch_policy() -> None:
    for relative_path in ACTIVE_MACOS_SOURCES:
        source = strip_c_comments(read(relative_path))
        for name, pattern in UNSAFE_PROCESS_PATTERNS.items():
            if pattern.search(source):
                raise AssertionError(f"Unsafe macOS process helper {name!r} found in {relative_path}")

    misc = read("src/sys/osx/macosx_misc.mm")
    start_process = function_body(misc, "static bool OSX_StartProcessArgs( char *const argv[], bool dofork ) {")
    do_start_process = function_body(misc, "void Sys_DoStartProcess( const char *exeName, bool dofork ) {")
    filtered_environment = function_body(misc, "static char **OSX_CreateFilteredProcessEnvironment() {")
    drop_environment = function_body(misc, "static bool OSX_ShouldDropProcessEnvironmentEntry( const char *entry ) {")

    require(misc, "static bool OSX_EnsureOwnerExecutable( const char *path )", "macOS executable-bit helper")
    require(start_process, "if ( !OSX_EnsureOwnerExecutable( argv[0] ) )", "macOS executable-bit verification")
    require(start_process, "executable bit is not set and could not be applied", "macOS executable-bit diagnostic")
    require(start_process, "!OSX_IsAbsolutePath( argv[0] ) || !OSX_IsRegularFile( argv[0] )", "macOS absolute process path policy")
    require(start_process, "execve( argv[0], argv, environment )", "macOS no-PATH exec policy")
    require(start_process, "posix_spawn( &childPid, argv[0], NULL, NULL, argv, environment )", "macOS no-PATH spawn policy")
    require(start_process, "char **environment = OSX_CreateFilteredProcessEnvironment();", "macOS filtered child environment")
    require(start_process, "free( environment );", "macOS filtered child environment cleanup")
    require(do_start_process, 'common->Printf( "Sys_DoStartProcess: invalid command line\\n" );', "macOS invalid process command diagnostic")
    reject(do_start_process, 'invalid command line \'%s\'', "macOS invalid process command diagnostic")
    require(filtered_environment, "calloc( keepCount + 1, sizeof( char * ) )", "macOS filtered child environment allocation")
    require(drop_environment, 'idStr::Cmpn( entry, "DYLD_", 5 ) == 0', "macOS DYLD environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_PRELOAD" )', "macOS loader environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_LIBRARY_PATH" )', "macOS loader environment filter")
    require(drop_environment, 'OSX_EnvironmentEntryHasName( entry, "LD_AUDIT" )', "macOS loader environment filter")


def validate_url_open_policy() -> None:
    open_url_hits = files_containing("openURL:")
    expected_hits = ["src/sys/osx/macosx_misc.mm"]
    if open_url_hits != expected_hits:
        raise AssertionError(f"Unexpected macOS openURL callsites: {open_url_hits!r}")

    misc = read("src/sys/osx/macosx_misc.mm")
    open_url = function_body(misc, "void idSysLocal::OpenURL( const char *url, bool doexit ) {")
    allowed_url = function_body(misc, "static bool OSX_IsAllowedURL( NSURL *url ) {")
    file_url = function_body(misc, "static bool OSX_FileURLIsLocalRuntimeFile( NSURL *url ) {")

    require(misc, "MAX_OSX_URL_LENGTH", "macOS URL length guard")
    require(open_url, "OpenURL rejected: expected a bounded URL with a safe scheme", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: URL is not valid UTF-8", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: Foundation could not parse URL", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL rejected: scheme is not allowed", "macOS unsafe URL diagnostic")
    require(open_url, "OpenURL failed after validation", "macOS URL handoff diagnostic")
    require_before(open_url, "OSX_URLHasSafeSchemeSyntax( url )", 'NSString *urlString = [ NSString stringWithUTF8String: url ];', "macOS URL syntax before UTF-8 bridge")
    require_before(open_url, "OSX_IsAllowedURL( nsURL )", 'common->Printf( "Open URL: %s\\n", url );', "macOS URL logging after allowlist guard")
    require_before(open_url, 'common->Printf( "Open URL: %s\\n", url );', "openURL: nsURL", "macOS approved URL logging before AppKit handoff")
    require_before(open_url, "OSX_IsAllowedURL( nsURL )", "openURL: nsURL", "macOS URL allowlist before AppKit handoff")
    reject(open_url, "OpenURL '%s' rejected", "macOS rejected URL raw logging")
    reject(open_url, "OpenURL '%s' failed", "macOS rejected URL raw logging")
    require(allowed_url, '[scheme caseInsensitiveCompare:@"https"]', "macOS URL scheme allowlist")
    require(allowed_url, '[scheme caseInsensitiveCompare:@"http"]', "macOS URL scheme allowlist")
    require(allowed_url, "return host != nil && [host length] > 0;", "macOS HTTP URL host guard")
    require(allowed_url, '[scheme caseInsensitiveCompare:@"file"]', "macOS URL scheme allowlist")
    require(file_url, "OSX_StringHasControlCharacters( path )", "macOS file URL path control-character guard")
    require(file_url, 'GetCVarString( "fs_savepath" )', "macOS file URL runtime-root policy")
    require(file_url, 'GetCVarString( "fs_basepath" )', "macOS file URL runtime-root policy")
    require(file_url, "OSX_ResolvedPathIsUnderDirectory( path, savePath )", "macOS file URL runtime-root policy")
    reject(misc, "static bool OSX_IsSafeURL", "macOS broad URL syntax-only policy")


def validate_deprecated_api_boundaries() -> None:
    nsrun_hits = files_containing("NSRunAlertPanel")
    if nsrun_hits:
        raise AssertionError(f"Deprecated NSRunAlertPanel found in active macOS sources: {nsrun_hits!r}")

    nsopengl_hits = files_containing("NSOpenGL")
    if nsopengl_hits != ["src/sys/osx/macosx_glimp.mm"]:
        raise AssertionError(f"NSOpenGL escaped the native fallback boundary: {nsopengl_hits!r}")

    carbon_hits = files_containing("Carbon")
    if carbon_hits != ["src/sys/osx/macosx_event.mm"]:
        raise AssertionError(f"Carbon escaped the native fallback boundary: {carbon_hits!r}")

    sources = read("tools/build/meson_sources.py")
    sdl3_darwin = assignment_block(sources, "SDL3_DARWIN_SOURCES")
    native_darwin = assignment_block(sources, "DARWIN_PLATFORM_SOURCES")
    require(native_darwin, '"sys/osx/macosx_event.mm"', "native macOS source list")
    require(native_darwin, '"sys/osx/macosx_glimp.mm"', "native macOS source list")
    reject(sdl3_darwin, "macosx_event.mm", "SDL3 macOS source list")
    reject(sdl3_darwin, "macosx_glimp.mm", "SDL3 macOS source list")

    meson = read("meson.build")
    require(meson, "macos_framework_modules = ['Cocoa', 'OpenGL', 'ApplicationServices']", "macOS SDL3 framework policy")
    require(meson, "if not use_sdl3_backend\n      macos_framework_modules += ['Carbon']", "macOS Carbon native-only policy")
    reject(meson, "modules: ['Cocoa', 'OpenGL', 'ApplicationServices', 'Carbon']", "macOS unconditional Carbon framework policy")
    require_before(meson, "if macos_openal_provider == 'apple_framework'", "dependency('appleframeworks', modules: ['OpenAL'], required: true)", "macOS OpenAL provider policy")
    require(meson, "dependency('openal', required: true)", "macOS system OpenAL migration policy")


def validate_release_distribution_policy() -> None:
    release = read(".github/workflows/manual-release.yml")
    package = read("tools/build/package_nightly.py")

    require(release, "--macos-signing-mode developer-id", "macOS release signing policy")
    require(release, "--macos-notarize", "macOS release notarization policy")
    require(release, "xcrun stapler validate", "macOS release stapling policy")
    require(release, "hdiutil verify", "macOS DMG validation policy")
    reject(release, "--macos-entitlements", "macOS release default entitlement policy")
    require(package, "MACOS_FORBIDDEN_ENTITLEMENTS", "macOS entitlement policy")
    require(package, "com.apple.security.app-sandbox", "macOS App Sandbox entitlement policy")
    require(package, "com.apple.security.get-task-allow", "macOS debug entitlement policy")


def validate_package_root_runtime_policy() -> None:
    compat = read("src/sys/osx/macosx_compat.mm")
    package_directory = function_body(compat, "static void Sys_RequireMacOSPackageRootDirectory( const idStr &packageDirectory, const char *entry, idStr &missingEntries ) {")
    package_executable = function_body(compat, "static void Sys_RequireMacOSPackageRootExecutable( const idStr &packageDirectory, const char *entry, idStr &missingEntries ) {")
    executable_exists = function_body(compat, "static bool Sys_ExecutableFileExists( const char *path ) {")
    path_exists = function_body(compat, "static bool Sys_MacOSPackageRootPathExists( const idStr &packageDirectory, const char *entry ) {")

    require(compat, "static bool Sys_PathIsSymlink( const char *path )", "macOS package-root symlink helper")
    require(compat, "lstat( path, &st )", "macOS package-root symlink helper")
    require(path_exists, "return lstat( testPath.c_str(), &st ) != -1;", "macOS package-root mismatched-entry symlink diagnostic")
    require(package_directory, "lstat( testPath.c_str(), &st )", "macOS package-root directory symlink guard")
    require(package_directory, 'Sys_AppendMacOSPackageRootIssue( missingEntries, entry, "symlink" )', "macOS package-root directory symlink diagnostic")
    require(package_executable, "lstat( testPath.c_str(), &st )", "macOS package-root executable symlink guard")
    require(package_executable, 'Sys_AppendMacOSPackageRootIssue( missingEntries, entry, "symlink" )', "macOS package-root executable symlink diagnostic")
    require(executable_exists, "!Sys_PathIsSymlink( path )", "macOS package-root executable symlink guard")


def validate_ci_wiring() -> None:
    script = "tools/tests/macos_static_policy.py"
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    status_ledger = read("docs/dev/prey-rebase/status-ledger.md")

    require(validator, "macos_static_policy.py", "validation runner")
    require(commit, script, "commit validation workflow")
    require(push, script, "push verification workflow")
    require(status_ledger, "TODO-RELEASE-LANES", "macOS release-lane deferral")


def main() -> None:
    validate_process_launch_policy()
    validate_url_open_policy()
    validate_deprecated_api_boundaries()
    validate_release_distribution_policy()
    validate_package_root_runtime_policy()
    validate_ci_wiring()
    print("macos_static_policy: ok")


if __name__ == "__main__":
    main()
