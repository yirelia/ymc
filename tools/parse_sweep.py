#!/usr/bin/env python3
"""S1 语法门扫描：对一个目录下所有 .mo 跑 `ymc --check-syntax`，按 `// status:` 分桶统计，
可选做 parse→unparse→parse→unparse 幂等检查（方案 §5 S1 验收 ①②）。

用法：
  python tools/parse_sweep.py --dir C:/dev/OpenModelica/testsuite/flattening/modelica
  python tools/parse_sweep.py --dir ... --roundtrip --jobs 8 --show 30
  python tools/parse_sweep.py --dir ... --only-status correct --json out.json

判定：
  OK       退出码 0（无 Error；Warning 允许）
  ERR      退出码 1（有语法错误）
  TIMEOUT  超时（默认 10 s）—— 视为运行器/解析器 bug，必须为 0
  CRASH    其他退出码（断言/崩溃）
  RT-FAIL  round-trip 两次 unparse 不一致（仅 --roundtrip，且仅对 OK 的文件做）
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict

STATUS_RE = re.compile(r"^//\s*status\s*:\s*(\w+)", re.IGNORECASE | re.MULTILINE)

_failing_cache = {}


def omc_failing_set(dirpath):
    """该目录 Makefile 里 FAILINGTESTFILES 列出的文件名集合（OMC 自己也跑不过的用例，不进验收门）。"""
    if dirpath in _failing_cache:
        return _failing_cache[dirpath]
    names = set()
    mk = os.path.join(dirpath, "Makefile")
    try:
        with open(mk, "r", encoding="utf-8", errors="replace") as f:
            text = f.read().replace("\\\n", " ")  # 续行合并
        m = re.search(r"^FAILINGTESTFILES\s*=\s*(.*)$", text, re.MULTILINE)
        if m:
            names = set(m.group(1).split())
    except OSError:
        pass
    _failing_cache[dirpath] = names
    return names


def read_status(path):
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            head = f.read(4096)
    except OSError:
        return "unknown"
    # OMC 自己标为 failing 的用例单独分桶（testsuite 的 rtest 不跑它们）
    if os.path.basename(path) in omc_failing_set(os.path.dirname(path)):
        return "omc-failing"
    m = STATUS_RE.search(head)
    return m.group(1).lower() if m else "none"


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", "", ""


def first_error(stderr):
    for line in stderr.splitlines():
        if "Error" in line:
            return line.strip()
    return stderr.strip().splitlines()[0] if stderr.strip() else ""


def check_one(ymc, path, timeout, roundtrip):
    status = read_status(path)
    rc, _, err = run([ymc, "--check-syntax", path], timeout)
    if rc == "timeout":
        return dict(path=path, status=status, verdict="TIMEOUT", msg="")
    if rc == 0:
        verdict, msg = "OK", ""
    elif rc == 1:
        verdict, msg = "ERR", first_error(err)
    else:
        verdict, msg = "CRASH", f"rc={rc} {first_error(err)}"
    if roundtrip and verdict == "OK":
        rc1, out1, _ = run([ymc, "--dump-after=parse", path], timeout)
        if rc1 == "timeout" or rc1 != 0:
            return dict(path=path, status=status, verdict="RT-FAIL", msg="dump1 failed")
        with tempfile.NamedTemporaryFile("w", suffix=".mo", delete=False,
                                         encoding="utf-8") as t1:
            t1.write(out1)
            t1_path = t1.name
        try:
            rc2, out2, err2 = run([ymc, "--dump-after=parse", t1_path], timeout)
        finally:
            try:
                os.unlink(t1_path)
            except OSError:
                pass
        if rc2 == "timeout" or rc2 != 0:
            return dict(path=path, status=status, verdict="RT-FAIL",
                        msg="reparse of unparse failed: " + first_error(err2))
        if out1 != out2:
            # 找第一处不同的行
            l1, l2 = out1.splitlines(), out2.splitlines()
            for i, (a, b) in enumerate(zip(l1, l2)):
                if a != b:
                    msg = f"line {i+1}: {a[:60]!r} vs {b[:60]!r}"
                    break
            else:
                msg = f"length {len(l1)} vs {len(l2)}"
            return dict(path=path, status=status, verdict="RT-FAIL", msg=msg)
    return dict(path=path, status=status, verdict=verdict, msg=msg)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--dir", required=True)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 1))
    ap.add_argument("--roundtrip", action="store_true")
    ap.add_argument("--only-status", default=None, help="只统计该 status（如 correct）")
    ap.add_argument("--show", type=int, default=20, help="列出前 N 个失败")
    ap.add_argument("--json", default=None)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    # Windows CreateProcess 不接受带正斜杠的相对路径，统一成绝对原生路径
    args.ymc = os.path.abspath(args.ymc)

    files = []
    for root, _, names in os.walk(args.dir):
        for n in names:
            if n.endswith(".mo"):
                files.append(os.path.join(root, n))
    files.sort()
    if args.limit:
        files = files[: args.limit]
    if args.only_status:
        files = [f for f in files if read_status(f) == args.only_status.lower()]
    if not files:
        print("no .mo files", file=sys.stderr)
        return 2

    t0 = time.time()
    results = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = [ex.submit(check_one, args.ymc, f, args.timeout, args.roundtrip) for f in files]
        for i, fut in enumerate(cf.as_completed(futs), 1):
            results.append(fut.result())
            if i % 200 == 0:
                print(f"  ... {i}/{len(files)}", file=sys.stderr)
    results.sort(key=lambda r: r["path"])

    by_status = defaultdict(Counter)
    total = Counter()
    for r in results:
        by_status[r["status"]][r["verdict"]] += 1
        total[r["verdict"]] += 1

    print(f"== parse sweep: {len(files)} files, {time.time()-t0:.1f}s, jobs={args.jobs}, "
          f"roundtrip={'on' if args.roundtrip else 'off'} ==")
    verdicts = ["OK", "ERR", "RT-FAIL", "TIMEOUT", "CRASH"]
    print(f"{'status':<12}" + "".join(f"{v:>9}" for v in verdicts) + f"{'total':>9}")
    for st in sorted(by_status, key=lambda s: -sum(by_status[s].values())):
        c = by_status[st]
        print(f"{st:<12}" + "".join(f"{c[v]:>9}" for v in verdicts) + f"{sum(c.values()):>9}")
    print(f"{'ALL':<12}" + "".join(f"{total[v]:>9}" for v in verdicts) + f"{len(results):>9}")

    # 关键指标：status=correct 的文件里 OK 的比例（S1 验收 ①）
    c_ok = by_status["correct"]["OK"]
    c_all = sum(by_status["correct"].values())
    if c_all:
        print(f"\nstatus=correct: {c_ok}/{c_all} OK ({100.0*c_ok/c_all:.1f}%)")

    fails = [r for r in results if r["verdict"] != "OK"]
    # 按错误消息聚类，看最常见的失败原因
    cause = Counter()
    for r in fails:
        m = re.sub(r"^\[\d+:\d+:\d+\]\s*", "", r["msg"])
        m = re.sub(r"'[^']*'$", "'…'", m)  # 抹掉具体记号名
        cause[(r["verdict"], m[:70])] += 1
    if cause:
        print("\n最常见失败原因：")
        for (v, m), n in cause.most_common(12):
            print(f"  {n:5d}  {v:<8} {m}")
    if fails and args.show:
        print(f"\n前 {min(args.show, len(fails))} 个失败：")
        for r in fails[: args.show]:
            rel = os.path.relpath(r["path"], args.dir)
            print(f"  {r['verdict']:<8} [{r['status']}] {rel}\n           {r['msg'][:110]}")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(results, f, ensure_ascii=False, indent=1)
    return 0 if (total["TIMEOUT"] == 0 and total["CRASH"] == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
