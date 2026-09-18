#!/usr/bin/env python3
"""S1 验收 ③：语法错误用例的错误位置与 OMC 对齐（方案 §5 S1 验收 ③，"位置一致即可，措辞不要求"）。

对语料里每个 .mo：从 `// Result:` 块提取 OMC 期望的 Error 位置（`[path:sl:sc-el:ec:writable] Error:`），
跑 `ymc --check-syntax` 取第一条错误的位置，比较 **起始 line:col**。

判定：
  MATCH      期望有语法错误且 ymc 第一条错误起点一致
  MISMATCH   期望有语法错误但 ymc 起点不同（打印两边的消息）
  MISSING    期望有语法错误但 ymc 没报错
  SPURIOUS   期望无语法错误（Result 里没有 Parse/Parser/Missing token/No viable）但 ymc 报错
  OK         期望无语法错误且 ymc 无错
  SKIP       无 Result 块 / .mos / MetaModelica（+g=MetaModelica）

用法：
  python tools/parse_error_align.py                       # 默认语料
  python tools/parse_error_align.py --dir <dir> --show 40
"""
import argparse
import os
import re
import subprocess
import sys
from collections import Counter

TS = "C:/dev/OpenModelica/testsuite"
DEFAULT_FILES = [
    f"{TS}/openmodelica/parser",
    f"{TS}/flattening/modelica/arrays/EmptyArray.mo",
    f"{TS}/flattening/modelica/modification/BreakComponentInvalid7.mo",
    f"{TS}/flattening/modelica/scodeinst/Ticket5821.mo",
]

# OMC 语法层错误的消息特征（其它 Error 是语义错误，不算）
PARSE_MSG = re.compile(r"Error: (Parse error|Parser error|Missing token|No viable alternative|"
                       r"Unexpected token|Lexer error|mismatched input|extraneous input)")
OMC_LOC = re.compile(r"\[[^\]\n]*?:(\d+):(\d+)-(\d+):(\d+):(?:writable|readonly)\]\s*(Error: .*)")
YMC_LOC = re.compile(r"\[[^\]\n]*?:(\d+):(\d+)-(\d+):(\d+):writable\]\s*(Error: .*)|\[(\d+):(\d+):(\d+)\]\s*(Error: .*)")


def read_text(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def expected_errors(text):
    """返回 Result 块里 OMC 的语法层错误 [(line, col, msg)]；None 表示无 Result 块。"""
    m = re.search(r"^// Result:\s*$(.*?)^// endResult", text, re.S | re.M)
    if not m:
        return None
    out = []
    for line in m.group(1).splitlines():
        line = line[3:] if line.startswith("// ") else line[2:] if line.startswith("//") else line
        lm = OMC_LOC.search(line)
        if lm and PARSE_MSG.search(line):
            out.append((int(lm.group(1)), int(lm.group(2)), lm.group(5).strip()))
    return out


CFLAGS_RE = re.compile(r"^//\s*cflags\s*:(.*)$", re.MULTILINE)


def ymc_flags_from_cflags(text):
    """把用例 `// cflags:` 里 ymc 认识的 flag 透传（目前只有 --std=X.Y）。"""
    m = CFLAGS_RE.search(text[:2000])
    if not m:
        return []
    flags = []
    for tok in m.group(1).split():
        if tok.startswith("--std=") or tok.startswith("+std="):
            flags.append("--std=" + tok.split("=", 1)[1])
    return flags


def ymc_errors(ymc, path, timeout, extra_flags=()):
    try:
        p = subprocess.run([ymc, "--check-syntax", *extra_flags, path], capture_output=True, text=True,
                           timeout=timeout, encoding="utf-8", errors="replace")
    except subprocess.TimeoutExpired:
        return "timeout", []
    errs = []
    for line in (p.stderr + p.stdout).splitlines():
        m = YMC_LOC.search(line)
        if not m:
            continue
        if m.group(1):
            errs.append((int(m.group(1)), int(m.group(2)), m.group(5).strip()))
        else:
            errs.append((int(m.group(6)), int(m.group(7)), m.group(9).strip()))
    return p.returncode, errs


def collect(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names if n.endswith(".mo")]
        elif p.endswith(".mo"):
            files.append(p)
    return sorted(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--dir", action="append", default=None, help="可多次给；默认 openmodelica/parser + 3 个 flattening 用例")
    ap.add_argument("--timeout", type=float, default=20.0)
    ap.add_argument("--show", type=int, default=60)
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)  # Windows CreateProcess 不接受带正斜杠的相对路径

    files = collect(args.dir or DEFAULT_FILES)
    counts = Counter()
    rows = []
    for f in files:
        text = read_text(f)
        rel = os.path.relpath(f, TS).replace(os.sep, "/")
        if "MetaModelica" in text[:400] or "+g=MetaModelica" in text or "-g=MetaModelica" in text:
            counts["SKIP"] += 1
            rows.append(("SKIP", rel, "MetaModelica", ""))
            continue
        exp = expected_errors(text)
        if exp is None:
            counts["SKIP"] += 1
            rows.append(("SKIP", rel, "no Result block", ""))
            continue
        rc, got = ymc_errors(args.ymc, f, args.timeout, ymc_flags_from_cflags(text))
        if rc == "timeout":
            counts["TIMEOUT"] += 1
            rows.append(("TIMEOUT", rel, "", ""))
            continue
        if not exp:
            if rc == 0:
                counts["OK"] += 1
            else:
                counts["SPURIOUS"] += 1
                rows.append(("SPURIOUS", rel, "(OMC: no parse error)", got[0][2] if got else f"rc={rc}"))
            continue
        e = exp[0]
        if not got:
            counts["MISSING"] += 1
            rows.append(("MISSING", rel, f"{e[0]}:{e[1]} {e[2]}", "(ymc: no error)"))
        elif (got[0][0], got[0][1]) == (e[0], e[1]):
            counts["MATCH"] += 1
        else:
            counts["MISMATCH"] += 1
            rows.append(("MISMATCH", rel, f"{e[0]}:{e[1]} {e[2]}", f"{got[0][0]}:{got[0][1]} {got[0][2]}"))

    print(f"== parse-error alignment: {len(files)} files ==")
    for k in ["MATCH", "OK", "MISMATCH", "MISSING", "SPURIOUS", "TIMEOUT", "SKIP"]:
        print(f"  {k:<9} {counts[k]}")
    judged = counts["MATCH"] + counts["MISMATCH"] + counts["MISSING"]
    if judged:
        print(f"\n语法错误用例位置一致率: {counts['MATCH']}/{judged} ({100.0*counts['MATCH']/judged:.1f}%)")
    bad = [r for r in rows if r[0] in ("MISMATCH", "MISSING", "SPURIOUS", "TIMEOUT")]
    if bad:
        print(f"\n需要处理的 {len(bad)} 个（显示前 {min(args.show, len(bad))}）：")
        for v, rel, e, g in bad[: args.show]:
            print(f"  {v:<9} {rel}")
            if e: print(f"            OMC: {e[:120]}")
            if g: print(f"            ymc: {g[:120]}")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
