#!/usr/bin/env python3
"""S1 性能基线（NFR-003 / 方案 §8 #5 的词法+语法部分）：单进程解析整个 MSL 4.0.0 的耗时，
与 OMC `loadModel(Modelica)`（解析 + 装载整个库）对照。取 N 次的最小值。

用法：
  python tools/bench_parse.py                # 默认 MSL 4.0.0+maint.om，3 次
  python tools/bench_parse.py --runs 5 --no-omc
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

MSL = "C:/Users/yangrui/AppData/Roaming/.openmodelica/libraries/Modelica 4.0.0+maint.om"


def collect(root):
    out = []
    for r, _, names in os.walk(root):
        out += [os.path.join(r, n) for n in names if n.endswith(".mo")]
    return sorted(out)


def count_lines_bytes(files):
    lines = 0
    nbytes = 0
    for f in files:
        with open(f, "rb") as fh:
            data = fh.read()
        nbytes += len(data)
        lines += data.count(b"\n")
    return lines, nbytes


def time_cmd(cmd, runs, cwd=None):
    best = None
    for _ in range(runs):
        t0 = time.perf_counter()
        p = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd,
                           encoding="utf-8", errors="replace")
        dt = time.perf_counter() - t0
        if p.returncode not in (0, 1):
            print(f"  warning: rc={p.returncode}: {p.stderr[:200]}", file=sys.stderr)
        best = dt if best is None else min(best, dt)
    return best, p


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--msl", default=MSL)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--no-omc", action="store_true")
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)  # Windows CreateProcess 不接受带正斜杠的相对路径

    files = collect(args.msl)
    lines, nbytes = count_lines_bytes(files)
    print(f"corpus: {len(files)} files, {lines} lines, {nbytes/1e6:.1f} MB")

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False, encoding="utf-8") as lst:
        lst.write("\n".join(files))
        lst_path = lst.name
    try:
        t_ymc, p = time_cmd([args.ymc, "--check-syntax", f"--files-from={lst_path}"], args.runs)
    finally:
        os.unlink(lst_path)
    tail = p.stderr.strip().splitlines()[-1] if p.stderr.strip() else ""
    print(f"ymc  lex+parse (single process, {args.runs} runs, min): {t_ymc:.2f} s  "
          f"→ {lines/t_ymc/1e3:.0f} k lines/s, {nbytes/t_ymc/1e6:.1f} MB/s   [{tail}]")

    if args.no_omc:
        return 0
    with tempfile.NamedTemporaryFile("w", suffix=".mos", delete=False, encoding="utf-8") as mos:
        mos.write('loadModel(Modelica, {"4.0.0"}); getErrorString();\n')
        mos_path = mos.name
    try:
        t_omc, p2 = time_cmd(["omc", mos_path], args.runs)
    finally:
        os.unlink(mos_path)
    ok = "true" in p2.stdout
    print(f"omc  loadModel(Modelica 4.0.0) (parse+load, {args.runs} runs, min): {t_omc:.2f} s   "
          f"[{'ok' if ok else 'FAILED: ' + p2.stdout[:80]}]")
    print(f"ratio ymc/omc: {t_ymc/t_omc:.2f}×  （注意：omc 的 loadModel 含库装载与进程启动，不是纯解析）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
