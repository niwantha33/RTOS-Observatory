#!/usr/bin/env python3
"""Baseline-aware, project-owned host diagnostics; never writes source files."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

SOURCE_DIRS = ("src", "include", "desktop", "firmware", "tests")
SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh"))
C_SOURCES = frozenset((".c", ".cc", ".cpp", ".cxx"))
# Lines and columns intentionally do not form part of the finding identity:
# inserting unrelated lines must not disguise pre-existing diagnostics.
LOCATION = re.compile(
    r"^(.+?):(\d+):(?:(\d+):)?\s*(error|warning):\s*(.*?)"
    r"(?:\s+\[([\w.-]+)\])?\s*$"
)


def owned_files(root: Path) -> list[Path]:
    return sorted(
        path for folder in SOURCE_DIRS
        for path in (root / folder).rglob("*")
        if path.is_file() and path.suffix.lower() in SUFFIXES
        and not any(part in ("third_party", "generated", "build") for part in path.parts)
    )


def normalized_path(root: Path, name: str) -> str | None:
    candidate = Path(name)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        relative = candidate.resolve().relative_to(root.resolve())
    except (ValueError, OSError):
        return None
    if not relative.parts or relative.parts[0] not in SOURCE_DIRS:
        return None
    if any(part in ("generated", "third_party", "build") for part in relative.parts):
        return None
    return relative.as_posix()


def finding(tool: str, path: str, severity: str, rule: str, message: str) -> dict:
    return {
        "tool": tool, "path": path, "severity": severity,
        "rule": rule, "message": " ".join(message.split()),
    }


def text_diagnostics(root: Path, output: str, tool: str) -> list[dict]:
    results = []
    for line in output.splitlines():
        match = LOCATION.match(line.strip())
        if not match:
            continue
        name, _line, _column, severity, message, rule = match.groups()
        path = normalized_path(root, name)
        if path is not None:
            results.append(finding(tool, path, severity, rule or "compiler", message))
    return results


def run(command: list[str], root: Path, timeout: int = 600) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(command, cwd=root, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"Tool failed: {command[0]}: {exc}") from exc


def clang_format(root: Path, source_files: list[Path], policy_root: Path) -> list[dict]:
    findings = []
    style = f"--style=file:{policy_root / '.clang-format'}"
    for path in source_files:
        result = run(["clang-format", style, "--dry-run", "--Werror", str(path)], root, 60)
        diagnostics = text_diagnostics(root, result.stderr, "clang-format")
        # Formatting failures are expected here; an unexplained failure is not.
        if result.returncode and not diagnostics:
            raise RuntimeError(f"clang-format failed on {path}: {result.stderr[-2000:]}")
        findings.extend(
            finding("clang-format", diagnostic["path"], "error", "format",
                    "code should be clang-formatted")
            for diagnostic in diagnostics if diagnostic["severity"] == "error"
        )
    return findings


def clang_tidy(root: Path, build: Path, policy_root: Path) -> list[dict]:
    database = build / "compile_commands.json"
    if not database.is_file():
        raise RuntimeError(f"Missing compilation database: {database}")
    records = json.loads(database.read_text(encoding="utf-8"))
    sources = sorted({
        str(Path(item["file"]).resolve()) for item in records
        if Path(item["file"]).suffix.lower() in C_SOURCES
        and normalized_path(root, str(item["file"])) is not None
    })
    if not sources:
        raise RuntimeError("No project-owned translation units in compile_commands.json")
    diagnostics = []
    for source in sources:
        result = run(["clang-tidy", "-quiet", f"-p={build}",
                      f"--config-file={policy_root / '.clang-tidy'}", source], root, 180)
        parsed = text_diagnostics(root, result.stdout + "\n" + result.stderr, "clang-tidy")
        if result.returncode:
            raise RuntimeError(
                f"clang-tidy failed ({result.returncode}) on {source}: "
                f"{(result.stderr + result.stdout)[-2500:]}"
            )
        diagnostics.extend(parsed)
    return diagnostics


def cppcheck(root: Path, source_files: list[Path]) -> list[dict]:
    cpp_sources = [str(path.relative_to(root)) for path in source_files
                   if path.suffix.lower() in C_SOURCES]
    if not cpp_sources:
        raise RuntimeError("No project C or C++ source files for Cppcheck")
    command = [
        "cppcheck", "--xml", "--xml-version=2", "--std=c11", "--std=c++20",
        "--enable=warning,performance,portability", "--suppress=missingIncludeSystem",
        "--suppress=missingInclude", "--inline-suppr", "-Iinclude", "-Ifirmware",
        "-Idesktop", *cpp_sources,
    ]
    result = run(command, root, 900)
    if result.returncode:
        raise RuntimeError(f"Cppcheck failed ({result.returncode}): {result.stderr[-2500:]}")
    try:
        report = ET.fromstring(result.stderr)
    except ET.ParseError as exc:
        raise RuntimeError(f"Cppcheck produced invalid XML: {exc}") from exc
    diagnostics = []
    for item in report.findall(".//error"):
        location = next(
            (normalized_path(root, loc.attrib.get("file", "")) for loc in item.findall("location")
             if normalized_path(root, loc.attrib.get("file", "")) is not None), None
        )
        if location is None:
            continue
        severity = "error" if item.get("severity") == "error" else "warning"
        diagnostics.append(finding("cppcheck", location, severity,
                                   item.get("id", "cppcheck"),
                                   item.get("msg", "Unspecified Cppcheck finding")))
    return diagnostics


def fingerprint(item: dict) -> tuple:
    return (item["tool"], item["path"], item["severity"], item["rule"], item["message"])


def compare_findings(baseline: list[dict], candidate: list[dict]) -> tuple[list[dict], list[dict]]:
    existing = Counter(fingerprint(item) for item in baseline)
    added_errors, added_warnings = [], []
    for item in candidate:
        key = fingerprint(item)
        if existing[key]:
            existing[key] -= 1
        elif item["severity"] == "error":
            added_errors.append(item)
        else:
            added_warnings.append(item)
    return added_errors, added_warnings


def write_report(destination: Path, source: Path, items: list[dict], metadata: dict) -> None:
    payload = {
        "schema_version": 1, "source": str(source), "tools": metadata,
        "findings": sorted(items, key=fingerprint),
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def compiler_versions(root: Path) -> dict:
    versions = {}
    for tool, args in (("clang-format", ["--version"]), ("clang-tidy", ["--version"]),
                       ("cppcheck", ["--version"]), ("clang++", ["--version"])):
        result = run([tool, *args], root, 30)
        if result.returncode:
            raise RuntimeError(f"Cannot query {tool} version")
        versions[tool] = result.stdout.splitlines()[0] if result.stdout.strip() else ""
    return versions


def ci_message(item: dict, severity: str) -> str:
    value = f"{item['tool']} {item['rule']}: {item['message']}"
    value = value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    return f"::{severity} file={item['path']}::{value}"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    collect = commands.add_parser("collect")
    collect.add_argument("--source", type=Path, required=True)
    collect.add_argument("--build", type=Path, required=True)
    collect.add_argument("--output", type=Path, required=True)
    collect.add_argument("--build-log", type=Path)
    compare = commands.add_parser("compare")
    compare.add_argument("--baseline", type=Path, required=True)
    compare.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args(argv)

    if args.command == "compare":
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        candidate = json.loads(args.candidate.read_text(encoding="utf-8"))
        if baseline.get("schema_version") != 1 or candidate.get("schema_version") != 1:
            raise RuntimeError("Incompatible or missing diagnostics report schema")
        added_errors, added_warnings = compare_findings(
            baseline["findings"], candidate["findings"]
        )
        for item in added_errors:
            print(ci_message(item, "error"))
        for item in added_warnings:
            print(ci_message(item, "warning"))
        print(f"Quality delta: {len(added_errors)} new errors, "
              f"{len(added_warnings)} new warnings (advisory)")
        return 1 if added_errors else 0

    source = args.source.resolve()
    build = args.build.resolve()
    policy_root = Path(__file__).resolve().parent.parent
    sources = owned_files(source)
    if not sources:
        raise RuntimeError(f"No owned code found: {source}")
    diagnostics = clang_format(source, sources, policy_root)
    diagnostics += clang_tidy(source, build, policy_root)
    diagnostics += cppcheck(source, sources)
    if args.build_log is not None:
        diagnostics += text_diagnostics(
            source, args.build_log.read_text(encoding="utf-8", errors="replace"),
            "compiler"
        )
    write_report(args.output.resolve(), source, diagnostics, compiler_versions(source))
    print(f"Collected {len(diagnostics)} findings from {len(sources)} owned files")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"::error::Quality gate unavailable: {error}", file=sys.stderr)
        sys.exit(2)
