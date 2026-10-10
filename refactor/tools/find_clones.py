#!/usr/bin/env python3
"""重复实现扫描器：对 C++ 源码做词法规范化后查找长重复片段。

用途：重构前的证据收集，只读源码，不修改任何文件。
输出：按重复规模排序的片段清单（文件、起始行、结束行、规范化词数）。
"""

from __future__ import annotations

import argparse
import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

KEYWORDS = {
    "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "continue", "default", "delete", "do", "double", "else",
    "enum", "explicit", "extern", "false", "float", "for", "friend", "goto", "if", "inline",
    "int", "long", "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
    "or", "override", "private", "protected", "public", "register", "return", "short",
    "signed", "sizeof", "static", "static_cast", "struct", "switch", "template", "this",
    "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual",
    "void", "volatile", "while", "std", "const_", "final", "and_eq", "bitand",
}

TOKEN_RE = re.compile(
    r"""
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<rawstring>R"[^()\s]*\(.*?\)[^()\s]*")
  | (?P<string>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
  | (?P<number>\b(?:0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][-+]?\d+)?[fFuUlL]*)\b)
  | (?P<ident>\b[A-Za-z_][A-Za-z_0-9]*\b)
  | (?P<ws>\s+)
  | (?P<op>::|->|<<|>>|<=|>=|==|!=|&&|\|\||\+\+|--|\+=|-=|\*=|/=|[^\s])
    """,
    re.DOTALL | re.VERBOSE,
)


@dataclass(frozen=True)
class Window:
    digest: str
    file: str
    line: int


def tokenize(text: str, *, normalize_identifiers: bool) -> list[tuple[str, int]]:
    """返回 (规范化词, 行号) 序列；注释与空白被丢弃。"""
    # 预处理指令（#include/#define/…）重复不代表实现重复，先整行去掉但保留行号。
    text = re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", "", text, flags=re.MULTILINE)
    tokens: list[tuple[str, int]] = []
    line = 1
    for match in TOKEN_RE.finditer(text):
        kind = match.lastgroup
        value = match.group()
        if kind == "ws" or kind == "comment":
            line += value.count("\n")
            continue
        if kind == "string" or kind == "rawstring":
            tokens.append(("S", line))
        elif kind == "number":
            tokens.append(("N", line))
        elif kind == "ident":
            if not normalize_identifiers or value in KEYWORDS:
                tokens.append((value, line))
            else:
                tokens.append(("I", line))
        else:
            tokens.append((value, line))
    return tokens


def digest_of(tokens: list[tuple[str, int]], start: int, size: int) -> str:
    joined = " ".join(tok for tok, _ in tokens[start:start + size])
    return hashlib.blake2b(joined.encode("utf-8"), digest_size=12).hexdigest()


def extend_match(tokens_a: list[tuple[str, int]], ia: int,
                 tokens_b: list[tuple[str, int]], ib: int) -> int:
    """从对齐位置向后比较规范化词序列，返回共同长度（词数）。"""
    length = 0
    limit = min(len(tokens_a) - ia, len(tokens_b) - ib)
    while length < limit and tokens_a[ia + length][0] == tokens_b[ib + length][0]:
        length += 1
    return length


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--window", type=int, default=30)
    parser.add_argument("--min-tokens", type=int, default=45)
    parser.add_argument("--top", type=int, default=60)
    parser.add_argument("--exact", action="store_true", help="不规范化标识符")
    args = parser.parse_args()

    files: list[Path] = []
    for raw in args.paths:
        path = Path(raw)
        if path.is_dir():
            files.extend(sorted(p for p in path.rglob("*") if p.suffix in {".cpp", ".h", ".hpp"}))
        else:
            files.append(path)

    per_file: dict[str, list[tuple[str, int]]] = {}
    for path in files:
        rel = str(path).replace("\\", "/")
        per_file[rel] = tokenize(path.read_text(encoding="utf-8", errors="replace"),
                                 normalize_identifiers=not args.exact)

    index: dict[str, list[tuple[str, int]]] = {}
    for rel, tokens in per_file.items():
        for start in range(len(tokens) - args.window + 1):
            index.setdefault(digest_of(tokens, start, args.window), []).append((rel, start))

    best: dict[tuple[str, str, int], tuple[int, int, int]] = {}
    for hits in index.values():
        if len(hits) < 2:
            continue
        for first in range(len(hits)):
            for second in range(first + 1, len(hits)):
                file_a, ia = hits[first]
                file_b, ib = hits[second]
                if file_a == file_b and ia == ib:
                    continue
                if file_a > file_b:
                    file_a, file_b, ia, ib = file_b, file_a, ib, ia
                length = extend_match(per_file[file_a], ia, per_file[file_b], ib)
                if length < args.min_tokens:
                    continue
                key = (file_a, file_b, ib - ia)
                previous = best.get(key)
                if previous is None or length > previous[0]:
                    best[key] = (length, ia, ib)

    regions: list[tuple[int, str, int, str, int]] = []
    for (file_a, file_b, _delta), (length, ia, ib) in best.items():
        regions.append((length, file_a, per_file[file_a][ia][1],
                        file_b, per_file[file_b][ib][1]))

    regions.sort(reverse=True)
    filtered: list[tuple[int, str, int, str, int]] = []
    accepted: list[tuple[str, str, int, int, int]] = []
    for length, file_a, first_line, file_b, second_line in regions:
        duplicate = False
        for other_a, other_b, other_length, other_first, other_second in accepted:
            if (other_a, other_b) != (file_a, file_b) or other_length < length:
                continue
            if abs(other_first - first_line) <= 3 and abs(other_second - second_line) <= 3:
                duplicate = True
                break
        if duplicate:
            continue
        accepted.append((file_a, file_b, length, first_line, second_line))
        filtered.append((length, file_a, first_line, file_b, second_line))
    regions = filtered
    total_tokens = sum(len(t) for t in per_file.values())
    print(f"扫描文件 {len(per_file)} 个，规范化词 {total_tokens} 个，"
          f"窗口 {args.window}，报告阈值 {args.min_tokens} 词")
    print(f"重复片段对 {len(regions)} 组\n")
    for span, file_a, line_a, file_b, line_b in regions[: args.top]:
        print(f"{span:5d} 词  {file_a}:{line_a}  <->  {file_b}:{line_b}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
