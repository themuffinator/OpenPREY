#!/usr/bin/env python3
"""Generate release notes for openPREY manual releases."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlparse


RELEASE_TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+)$")
VERSION_TAG_RE = re.compile(r"^\d+\.\d+\.\d+$")
REPO_SLUG_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
MARKDOWN_URL_UNSAFE_RE = re.compile(r"[\x00-\x20\x7f<>()\[\]`]")


def run_git(source_root: Path, args: list[str]) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(source_root), *args],
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(exc.stderr.strip() or exc.stdout.strip()) from exc
    return result.stdout.strip()


def parse_tag_version(tag: str) -> tuple[int, int, int] | None:
    match = RELEASE_TAG_RE.fullmatch(tag)
    if match is None:
        return None
    parts = match.group(1).split(".")
    return tuple(int(part) for part in parts)


def find_previous_release_tag(source_root: Path, current_release_tag: str) -> str | None:
    current_version = parse_tag_version(current_release_tag)
    if current_version is None:
        raise RuntimeError(f"invalid release tag: {current_release_tag!r}")

    tags = run_git(source_root, ["tag", "--list", "v*"])
    release_tags: list[tuple[tuple[int, int, int], str]] = []
    for raw_tag in tags.splitlines():
        tag = raw_tag.strip()
        if not tag or tag == current_release_tag:
            continue
        version = parse_tag_version(tag)
        if version is None or version >= current_version:
            continue
        release_tags.append((version, tag))

    if not release_tags:
        return None
    release_tags.sort()
    return release_tags[-1][1]


def collect_commits(
    source_root: Path,
    range_spec: str | None,
    max_count: int,
) -> list[tuple[str, str, str, str]]:
    if max_count <= 0:
        return []

    pretty = "%H%x1f%h%x1f%ad%x1f%s"
    args = ["log", "--no-merges", "--date=short", "-n", str(max_count), f"--pretty=format:{pretty}"]
    if range_spec:
        args.append(range_spec)

    output = run_git(source_root, args)
    commits: list[tuple[str, str, str, str]] = []
    if not output:
        return commits

    for line in output.splitlines():
        full, short, date, subject = line.split("\x1f", 3)
        commits.append((full, short, date, subject.strip()))
    return commits


def select_highlights(commits: list[tuple[str, str, str, str]], max_items: int) -> list[str]:
    if max_items <= 0:
        return []

    seen: set[str] = set()
    highlights: list[str] = []
    for _, _, _, subject in commits:
        if not subject or subject in seen:
            continue
        seen.add(subject)
        highlights.append(subject)
        if len(highlights) >= max_items:
            break
    return highlights


def find_tracked_release_notes(
    source_root: Path,
    release_tag: str,
    version_tag: str,
) -> Path | None:
    candidates = (
        Path("docs/dev") / "releases" / f"{release_tag}.md",
        Path("docs/dev") / "releases" / f"{version_tag}.md",
    )
    for relative in candidates:
        candidate = source_root / relative
        if candidate.is_symlink():
            raise RuntimeError(f"tracked release notes must not be a symlink: {relative.as_posix()}")
        if candidate.is_file():
            return relative
    return None


def sanitize_release_notes_override(body: str, version_tag: str, release_tag: str) -> str:
    lines = body.splitlines()
    while lines and not lines[0].strip():
        lines.pop(0)

    if lines:
        title_pattern = re.compile(
            rf"^#{{1,6}}\s+openPREY\s+(?:{re.escape(version_tag)}|{re.escape(release_tag)})"
            rf"(?:\s+Release(?:\s+Notes)?)?\s*$",
            re.IGNORECASE,
        )
        if title_pattern.fullmatch(lines[0].strip()):
            lines.pop(0)
            while lines and not lines[0].strip():
                lines.pop(0)

    return "\n".join(lines).strip()


def normalize_release_text(value: str) -> str:
    normalized = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]", "", value)
    normalized = re.sub(r"\s+", " ", normalized.replace("\r", " ").replace("\n", " "))
    return normalized.strip()


def escape_markdown_inline(value: str) -> str:
    escaped = normalize_release_text(value).replace("\\", "\\\\")
    for char in ("`", "*", "_", "[", "]", "(", ")", "|"):
        escaped = escaped.replace(char, "\\" + char)
    return escaped


def escape_markdown_table_cell(value: str) -> str:
    return escape_markdown_inline(value)


def validate_repo_slug(repo: str) -> str:
    normalized = repo.strip()
    if REPO_SLUG_RE.fullmatch(normalized) is None:
        raise RuntimeError(
            "GitHub repository slug must be owner/name using only letters, digits, '.', '_', or '-'"
        )
    owner, name = normalized.split("/", 1)
    if owner in {".", ".."} or name in {".", ".."} or ".." in owner or ".." in name:
        raise RuntimeError(f"invalid GitHub repository slug: {repo!r}")
    return normalized


def validate_release_tag(release_tag: str) -> str:
    normalized = release_tag.strip()
    if RELEASE_TAG_RE.fullmatch(normalized) is None:
        raise RuntimeError(f"release tag must look like v0.1.010, got: {release_tag!r}")
    return normalized


def validate_version_tag(version_tag: str) -> str:
    normalized = version_tag.strip()
    if VERSION_TAG_RE.fullmatch(normalized) is None:
        raise RuntimeError(f"version tag must look like 0.1.010, got: {version_tag!r}")
    return normalized


def validate_optional_https_url(value: str, label: str) -> str:
    normalized = value.strip()
    if not normalized:
        return ""

    parsed = urlparse(normalized)
    if parsed.scheme != "https" or not parsed.netloc:
        raise RuntimeError(f"{label} must be an absolute https URL, got: {value!r}")
    if MARKDOWN_URL_UNSAFE_RE.search(normalized):
        raise RuntimeError(f"{label} contains characters that are unsafe in generated Markdown links: {value!r}")
    return normalized


def non_negative_int(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected a non-negative integer, got: {value!r}") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError(f"expected a non-negative integer, got: {value!r}")
    return parsed


def validate_output_path(output_path: Path) -> Path:
    raw_output = output_path
    if raw_output.is_symlink():
        raise RuntimeError(f"refusing to write release changelog through symlink: {raw_output}")
    parent = raw_output.parent if raw_output.parent != Path("") else Path(".")
    if parent.exists():
        if parent.is_symlink():
            raise RuntimeError(f"refusing to write release changelog into symlinked directory: {parent}")
        if not parent.is_dir():
            raise RuntimeError(f"release changelog output parent is not a directory: {parent}")
    return raw_output


def build_release_header(
    *,
    version: str,
    version_tag: str,
    release_tag: str,
    release_scale: str,
    release_reason: str,
    repo_url: str,
    head_sha: str,
    generated_at: str,
    run_id: str,
    run_url: str,
    previous_tag: str | None,
) -> list[str]:
    short_sha = head_sha[:8]
    previous_tag_link = ""
    compare_link = ""
    if previous_tag:
        previous_tag_link = f"[`{previous_tag}`]({repo_url}/releases/tag/{previous_tag})"
        compare_link = f"[compare]({repo_url}/compare/{previous_tag}...{head_sha})"

    lines: list[str] = []
    lines.append(f"## openPREY {version_tag}")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    lines.append(f"| Version | `{escape_markdown_table_cell(version)}` |")
    lines.append(f"| Release tag | `{escape_markdown_table_cell(release_tag)}` |")
    lines.append(f"| Commit | [`{short_sha}`]({repo_url}/commit/{head_sha}) |")
    lines.append(f"| Generated | `{escape_markdown_table_cell(generated_at)}` |")
    if release_scale:
        lines.append(f"| Release scale | `{escape_markdown_table_cell(release_scale)}` |")
    if release_reason:
        lines.append(f"| Version decision | {escape_markdown_table_cell(release_reason)} |")
    if run_url:
        run_label = run_id if run_id else "Workflow run"
        lines.append(f"| Workflow | [{escape_markdown_inline(run_label)}]({run_url}) |")
    if previous_tag:
        lines.append(f"| Since | {previous_tag_link} ({compare_link}) |")
    lines.append("")
    return lines


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate openPREY release notes.")
    parser.add_argument("--version", required=True, help="Human-readable release version.")
    parser.add_argument("--version-tag", required=True, help="File-safe release version tag.")
    parser.add_argument("--release-tag", required=True, help="Release tag (for example v0.1.010).")
    parser.add_argument("--release-scale", default="", help="Release scale emitted by the version helper.")
    parser.add_argument("--release-reason", default="", help="Release-version rationale emitted by the version helper.")
    parser.add_argument("--repo", required=True, help="GitHub repository slug (owner/name).")
    parser.add_argument("--output", required=True, help="Output markdown path.")
    parser.add_argument(
        "--source-root",
        default=".",
        help="Repository root used to resolve tracked release-notes overrides (default: current directory).",
    )
    parser.add_argument("--run-id", default="", help="GitHub workflow run ID.")
    parser.add_argument("--run-url", default="", help="GitHub workflow run URL.")
    parser.add_argument(
        "--max-commits",
        type=non_negative_int,
        default=40,
        help="Maximum commits to include in the change log section (default: 40).",
    )
    parser.add_argument(
        "--max-highlights",
        type=non_negative_int,
        default=6,
        help="Maximum highlighted commits (default: 6).",
    )
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    source_root = Path(args.source_root).resolve()
    repo_slug = validate_repo_slug(args.repo)
    version_tag = validate_version_tag(args.version_tag)
    release_tag = validate_release_tag(args.release_tag)
    run_url = validate_optional_https_url(args.run_url, "workflow run URL")

    repo_url = f"https://github.com/{repo_slug}"
    head_sha = run_git(source_root, ["rev-parse", "HEAD"])
    previous_tag = find_previous_release_tag(source_root, release_tag)
    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    lines = build_release_header(
        version=args.version,
        version_tag=version_tag,
        release_tag=release_tag,
        release_scale=args.release_scale,
        release_reason=args.release_reason,
        repo_url=repo_url,
        head_sha=head_sha,
        generated_at=generated_at,
        run_id=args.run_id,
        run_url=run_url,
        previous_tag=previous_tag,
    )

    release_notes_override = find_tracked_release_notes(
        source_root,
        release_tag,
        version_tag,
    )
    used_override = False
    if release_notes_override is not None:
        override_path = source_root / release_notes_override
        override_body = sanitize_release_notes_override(
            override_path.read_text(encoding="utf-8"),
            version_tag,
            release_tag,
        )
        if override_body:
            lines.extend(override_body.splitlines())
            lines.append("")
            used_override = True
            print(f"Using tracked release notes from {release_notes_override.as_posix()}")
        else:
            print(
                f"Tracked release notes file {release_notes_override.as_posix()} was empty; "
                "falling back to generated commit history."
            )

    if not used_override:
        commit_range = f"{previous_tag}..HEAD" if previous_tag else None
        commits = collect_commits(source_root, commit_range, args.max_commits)
        if not commits:
            commits = collect_commits(source_root, None, args.max_commits)

        highlights = select_highlights(commits, args.max_highlights)

        lines.append("### Highlights")
        lines.append("")
        if highlights:
            for subject in highlights:
                lines.append(f"- {escape_markdown_inline(subject)}")
        else:
            lines.append("- Maintenance and release integration updates.")
        lines.append("")

        lines.append("### Change Log")
        lines.append("")
        if commits:
            for full_sha, short, date, subject in commits[: args.max_commits]:
                lines.append(
                    f"- {escape_markdown_inline(subject)} ([`{short}`]({repo_url}/commit/{full_sha}), {date})"
                )
        else:
            lines.append("- No commit metadata was available for this release.")
        lines.append("")

    output_path = validate_output_path(Path(args.output))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote release changelog to {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
