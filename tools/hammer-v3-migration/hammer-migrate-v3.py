#!/usr/bin/env python3
"""Preview or apply mechanical Hammer 2.x to 3.x source migrations."""

import argparse
import difflib
from pathlib import Path
import re
import sys


VERSION = "1.0"
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
SKIP_DIRS = {".git", ".hg", ".svn", "build", "node_modules", "vendor"}
MEMBER_GROUPS = {
    "token_data": {"bytes", "dbl", "flt", "seq", "sint", "uint", "user"},
    "timestamp": {"actual_results", "parse_time"},
}
MEMBER_ACCESS = re.compile(
    r"(?=(?P<receiver>[A-Za-z_]\w*(?:\s*\[[^\]]+\])*)\s*"
    r"(?P<operator>->|\.)\s*"
    r"(?P<member>[A-Za-z_]\w*))"
)


def code_ranges(source):
    """Yield ranges containing C/C++ code, excluding comments and literals."""
    start = 0
    index = 0
    state = "code"
    while index < len(source):
        pair = source[index : index + 2]
        char = source[index]
        if state == "code":
            if pair in {"//", "/*"} or char in {'"', "'"}:
                if start < index:
                    yield start, index
                state = {"//": "line", "/*": "block"}.get(pair, "string")
                index += 2 if pair in {"//", "/*"} else 1
                continue
        elif state == "line":
            if char == "\n":
                state = "code"
                start = index
        elif state == "block":
            if pair == "*/":
                index += 2
                state = "code"
                start = index
                continue
        elif state == "string":
            if char == "\\":
                index += 2
                continue
            if char in {'"', "'"}:
                index += 1
                state = "code"
                start = index
                continue
        index += 1
    if state == "code" and start < len(source):
        yield start, len(source)


def code_only(source):
    """Return source with non-code characters replaced, preserving offsets."""
    result = [" "] * len(source)
    for begin, end in code_ranges(source):
        result[begin:end] = source[begin:end]
    return "".join(result)


def declared_names(source, type_name):
    """Return names from simple declarations of a public Hammer type."""
    declaration = re.compile(rf"\b{type_name}\b(?P<declarators>[^;{{}}]+);", re.DOTALL)
    names = set()
    for match in declaration.finditer(source):
        for declarator in match.group("declarators").split(","):
            name = re.search(r"(?:\*\s*)?([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)?$", declarator)
            if name:
                names.add(name.group(1))
    return names


def receiver_name(receiver):
    """Return the base identifier from a direct or indexed receiver."""
    return re.match(r"[A-Za-z_]\w*", receiver).group(0)


def migrate_source(source):
    edits = []
    counts = {group: 0 for group in MEMBER_GROUPS}
    code = code_only(source)
    receivers = {
        "token_data": declared_names(code, "HParsedToken"),
        "timestamp": declared_names(code, "HCaseResult"),
    }
    parse_results = declared_names(code, "HParseResult")

    ast_access = re.compile(
        r"(?=(?P<result>[A-Za-z_]\w*)\s*(?:->|\.)\s*ast\s*->\s*"
        r"(?P<member>[A-Za-z_]\w*))"
    )
    for match in ast_access.finditer(code):
        if match.group("result") not in parse_results:
            continue
        member = match.group("member")
        if member not in MEMBER_GROUPS["token_data"]:
            continue
        member_start = match.start("member")
        edits.append((member_start, member_start, "token_data."))
        counts["token_data"] += 1

    for match in MEMBER_ACCESS.finditer(code):
        member = match.group("member")
        receiver = receiver_name(match.group("receiver"))
        group = next(
            (
                name
                for name, fields in MEMBER_GROUPS.items()
                if member in fields and receiver in receivers[name]
            ),
            None,
        )
        if group is None:
            continue
        member_start = match.start("member")
        edits.append((member_start, member_start, f"{group}."))
        counts[group] += 1

    migrated = source
    for begin, end, replacement in reversed(edits):
        migrated = migrated[:begin] + replacement + migrated[end:]
    return migrated, counts


def source_files(paths):
    seen = set()
    for raw_path in paths:
        path = Path(raw_path)
        if not path.exists():
            raise FileNotFoundError(raw_path)
        candidates = [path] if path.is_file() else path.rglob("*")
        for candidate in candidates:
            if any(part in SKIP_DIRS for part in candidate.parts):
                continue
            if candidate.is_file() and candidate.suffix.lower() in SOURCE_SUFFIXES:
                resolved = candidate.resolve()
                if resolved not in seen:
                    seen.add(resolved)
                    yield candidate


def unified_diff(path, old, new):
    return "".join(
        difflib.unified_diff(
            old.splitlines(keepends=True),
            new.splitlines(keepends=True),
            fromfile=str(path),
            tofile=str(path),
        )
    )


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Preview or apply safe mechanical Hammer 2.x to 3.x source migrations."
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--write", action="store_true", help="modify files in place")
    mode.add_argument("--check", action="store_true", help="exit 1 if changes are needed")
    parser.add_argument("--version", action="version", version=f"%(prog)s {VERSION}")
    parser.add_argument("paths", nargs="+", help="C/C++ files or directories to inspect")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    changed = 0
    replacements = 0
    try:
        files = list(source_files(args.paths))
    except FileNotFoundError as error:
        print(f"hammer-migrate-v3: path does not exist: {error}", file=sys.stderr)
        return 2

    for path in files:
        try:
            with path.open("r", encoding="utf-8", newline="") as source_file:
                old = source_file.read()
        except (OSError, UnicodeError) as error:
            print(f"hammer-migrate-v3: cannot read {path}: {error}", file=sys.stderr)
            return 2
        new, counts = migrate_source(old)
        if new == old:
            continue
        changed += 1
        replacements += sum(counts.values())
        if args.write:
            try:
                with path.open("w", encoding="utf-8", newline="") as source_file:
                    source_file.write(new)
            except OSError as error:
                print(f"hammer-migrate-v3: cannot write {path}: {error}", file=sys.stderr)
                return 2
            print(f"updated {path} ({sum(counts.values())} replacements)")
        elif not args.check:
            sys.stdout.write(unified_diff(path, old, new))

    action = "updated" if args.write else "would update"
    print(f"hammer-migrate-v3: {action} {changed} file(s), {replacements} replacement(s)")
    if changed:
        print("Review and test the result: C/C++ member names are not globally type-unique.")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
