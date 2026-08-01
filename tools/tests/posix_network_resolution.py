#!/usr/bin/env python3
"""Regression checks for POSIX network address resolution."""

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


def validate_posix_resolver() -> None:
    source = read("src/sys/posix/posix_net.cpp")

    for legacy_symbol in ("gethostbyname", "inet_aton", "StringToSockaddr"):
        reject(source, legacy_symbol, "POSIX network resolver")

    for required_symbol in (
        "getaddrinfo",
        "freeaddrinfo",
        "AI_NUMERICHOST",
        "sockaddr_storage",
        "AF_INET6",
        "NA_IP6",
        "inet_ntop",
        "IPV6_V6ONLY",
        "netSocket6",
    ):
        require(source, required_symbol, "POSIX IPv6-capable network path")

    require(source, "Sys_SplitHostAndPort", "IPv6 bracket host parsing")
    require(source, "SockadrToNetadr", "sockaddr-to-netadr conversion")
    require(source, "NetadrToSockadr", "netadr-to-sockaddr conversion")
    require(source, "recvfrom( socketFd", "shared UDP receive helper")
    require(source, "sendto( socketFd", "family-selected UDP send")
    require(source, "select( maxSocket + 1", "dual-socket blocking receive")
    reject(source, "struct sockaddr_in sadr", "TCP resolver")
    reject(source, "struct sockaddr_in from", "UDP receive path")
    reject(source, "struct sockaddr_in addr", "UDP send path")


def validate_public_netadr_shape() -> None:
    source = read("src/sys/sys_public.h")

    for required_symbol in ("NA_IP6", "ip6[16]", "scopeId", "netSocket6"):
        require(source, required_symbol, "public net address shape")


def validate_legacy_message_format() -> None:
    source = read("src/idlib/BitMsg.cpp")

    require(source, "adr.type == NA_IP || adr.type == NA_LOOPBACK", "legacy netadr write filter")
    require(source, "memset( adr, 0, sizeof( *adr ) );", "legacy netadr read initialization")
    require(source, "adr->type = NA_IP;", "legacy netadr read type")


def main() -> None:
    validate_posix_resolver()
    validate_public_netadr_shape()
    validate_legacy_message_format()
    print("posix_network_resolution: ok")


if __name__ == "__main__":
    main()
