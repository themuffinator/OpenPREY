#!/usr/bin/env python3
"""Regression checks for cooperative POSIX thread shutdown."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_public_thread_state() -> None:
    source = read("src/sys/sys_public.h")

    require(source, "uintptr_t\t\tthreadHandle;", "pointer-sized thread handle")
    require(source, "volatile bool\tstopRequested;", "public cooperative stop flag")
    require(source, "Sys_RequestThreadStop", "thread stop request API")
    require(source, "Sys_IsThreadStopRequested", "thread stop query API")
    require(source, "Sys_IsCurrentThreadStopRequested", "current thread stop query API")


def validate_posix_thread_teardown() -> None:
    source = read("src/sys/posix/posix_threads.cpp")

    reject(source, "pthread_cancel", "POSIX thread teardown")
    reject(source, "pthread_create( ( pthread_t* )&info.threadHandle", "POSIX thread handle storage")
    require(source, "pthread_join( thread, NULL )", "POSIX cooperative join")
    require(source, "Sys_RequestThreadStop( info );", "POSIX destroy stop request")
    require(source, "Sys_PThreadToHandle", "POSIX pointer-sized pthread storage")
    require(source, "Sys_HandleToPThread", "POSIX pthread restoration")
    require(source, "pthread_equal", "POSIX pthread comparison")
    require(source, "Sys_RemoveThreadInfo", "POSIX thread table cleanup")


def validate_workers_observe_stop_requests() -> None:
    linux_main = read("src/sys/linux/main.cpp")
    macos_compat = read("src/sys/osx/macosx_compat.mm")
    macos_controller = read("src/sys/osx/DOOMController.mm")
    file_system = read("src/framework/FileSystem.cpp")
    lightgrid = read("src/renderer/RenderWorld_lightgrid.cpp")

    reject(linux_main, "pthread_testcancel", "Linux async thread")
    reject(macos_compat, "pthread_testcancel", "macOS async thread")
    reject(macos_controller, "pthread_testcancel", "legacy macOS async thread")

    for source, context in (
        (linux_main, "Linux async thread"),
        (macos_compat, "macOS async thread"),
        (macos_controller, "legacy macOS async thread"),
        (file_system, "background download thread"),
        (lightgrid, "light-grid bake workers"),
    ):
        require(source, "Sys_IsCurrentThreadStopRequested()", context)


def validate_cross_platform_shims() -> None:
    win32 = read("src/sys/win32/win_main.cpp")
    stub = read("src/sys/stub/sys_stub.cpp")

    require(win32, "reinterpret_cast<uintptr_t>(temp)", "Win32 pointer-sized thread handle")
    require(win32, "Sys_RequestThreadStop", "Win32 thread stop helper")
    require(win32, "Sys_IsCurrentThreadStopRequested", "Win32 current thread stop helper")
    require(stub, "Sys_RequestThreadStop", "stub thread stop helper")
    require(stub, "Sys_IsCurrentThreadStopRequested", "stub current thread stop helper")


def validate_sdl_main_thread_contract() -> None:
    source = read("src/sys/posix/posix_main.cpp")

    require(source, "Skipping SDL clipboard read from a non-main thread.", "SDL clipboard read guard")
    require(source, "Skipping SDL clipboard write from a non-main thread.", "SDL clipboard write guard")
    if source.count("if ( !Posix_IsMainThread() )") < 3:
        raise AssertionError("POSIX SDL teardown and both clipboard operations must reject worker-thread calls")


def main() -> None:
    validate_public_thread_state()
    validate_posix_thread_teardown()
    validate_workers_observe_stop_requests()
    validate_cross_platform_shims()
    validate_sdl_main_thread_contract()
    print("posix_thread_shutdown: ok")


if __name__ == "__main__":
    main()
