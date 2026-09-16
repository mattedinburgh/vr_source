from __future__ import annotations
import argparse
import csv
import re
from pathlib import Path

CALL_RE = re.compile(r"\b(StatChange|ProfileStatChange)\s*\(")
SOURCE_SUFFIXES = {".cpp", ".h"}

def split_args(text: str) -> list[str]:
    args, start, depth = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(text[start:i].strip())
            start = i + 1
    args.append(text[start:].strip())
    return args

def iter_calls(path: Path):
    text = path.read_text(encoding="utf-8", errors="ignore")
    for match in CALL_RE.finditer(text):
        prefix = text[max(0, match.start() - 16):match.start()]
        if re.search(r"\bvoid\s*$", prefix):
            continue
        depth, end = 1, match.end()
        while end < len(text) and depth:
            ch = text[end]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            end += 1
        if depth:
            continue
        args = split_args(text[match.end():end - 1])
        if len(args) < 4:
            continue
        line = text.count("\n", 0, match.start()) + 1
        context_start = text.rfind("\n", 0, match.start()) + 1
        context_end = text.find("\n", end)
        if context_end < 0:
            context_end = len(text)
        context = " ".join(text[context_start:context_end].split())
        yield {
            "file": str(path),
            "line": line,
            "call": match.group(1),
            "stat": args[1],
            "chances": args[2],
            "reason": args[3],
            "context": context,
        }

def main() -> int:
    parser = argparse.ArgumentParser(description="Audit JA2 stat-award call sites.")
    parser.add_argument("root", nargs="?", default=".", help="source root")
    parser.add_argument("--csv", dest="csv_path", help="optional CSV output")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    rows = []
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES:
            if ".git" in path.parts:
                continue
            for row in iter_calls(path):
                row["file"] = str(path.relative_to(root))
                rows.append(row)

    rows.sort(key=lambda r: (r["file"].lower(), r["line"]))
    fields = ["file", "line", "call", "stat", "chances", "reason", "context"]
    if args.csv_path:
        out = Path(args.csv_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        with out.open("w", newline="", encoding="utf-8-sig") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)

    print(f"Stat-award call sites: {len(rows)}")
    for row in rows:
        print(f"{row['file']}:{row['line']} | {row['call']} | "
              f"{row['stat']} | {row['chances']} | {row['reason']}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
