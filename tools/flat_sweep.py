#!/usr/bin/env python3
"""S3 压平门扫描：对一个 testsuite 目录并行跑 `ymc X.mo`，与文件内 `// Result:` 逐行比对（方案 §5 S3、§6.1/§6.2）。

判定复用 tests/omc/run.py 的 rtest 复刻（同一套归一化 + omc_diff 容差），这里只加三样：并行、按用例名前缀分桶、
FAIL/SKIP 原因汇总。三类结果：
  PASS  输出与期望一致
  FAIL  输出不一致 / 崩溃 / 超时（这是 bug）
  SKIP  ymc 以退出码 66 声明"该特性未实现"，或旧前端用例、被 cflags 规则跳过（这是进度，不是 bug）

用法：
  python tools/flat_sweep.py                                   # 默认扫 flattening/modelica/scodeinst（新前端语料）
  python tools/flat_sweep.py --dir <目录> --jobs 8 --show 30
  python tools/flat_sweep.py --cases-from docs/research/s3-easy-slice.json
  python tools/flat_sweep.py --only Inner,Import --status correct --json out.json
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tests", "omc"))
import run as rt  # noqa: E402  （tests/omc/run.py）

DEFAULT_DIR = os.path.join(rt.TESTSUITE_ROOT, "flattening", "modelica", "scodeinst")
_PREFIX = re.compile(r"[A-Z]?[a-z]+|[A-Z]+")


def prefix_of(path):
    m = _PREFIX.match(os.path.basename(path))
    return m.group(0) if m else "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--ymc", default=None, help="ymc 可执行文件（默认 build/gcc-debug/ymc.exe）")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--cases-from", help="JSON 文件：只跑列表里的文件名")
    ap.add_argument("--only", help="只跑这些前缀桶（逗号分隔）")
    ap.add_argument("--status", help="只跑该 status（correct / incorrect）")
    ap.add_argument("--show", type=int, default=20, help="列出前 N 个 FAIL 的首个差异")
    ap.add_argument("--show-skips", action="store_true", help="列出 SKIP 原因分布")
    ap.add_argument("--oracle-live", action="store_true",
                    help="把每个 PASS 用例再和**实跑 omc 1.25.0** 逐字节对拍（权威 oracle，非文件 Result）")
    ap.add_argument("--omc", default="omc", help="omc 可执行文件（默认 PATH 上的安装版 1.25.0）")
    ap.add_argument("--json")
    args = ap.parse_args()
    if args.ymc:
        rt.YMC = os.path.abspath(args.ymc)

    cases = [p for p in rt.discover(args.dir) if p.endswith(".mo")]
    if args.cases_from:
        with open(args.cases_from, encoding="utf-8") as f:
            wanted = set(json.load(f))
        cases = [p for p in cases if os.path.basename(p) in wanted]
    if args.only:
        only = set(args.only.split(","))
        cases = [p for p in cases if prefix_of(p) in only]
    if args.status:
        keep = []
        for p in cases:
            with open(p, encoding="utf-8", errors="replace") as f:
                st = rt.parse_directives(f.read()).get("status", "correct").strip().lower()
            if st == args.status:
                keep.append(p)
        cases = keep
    if not cases:
        print("no cases")
        return 2

    cmap = rt.load_cflags_map()
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        records = list(ex.map(lambda p: rt.run_case(p, "ymc", "installed", cmap, args.timeout), cases))

    tot = collections.Counter(r["verdict"] for r in records)
    buckets = collections.defaultdict(collections.Counter)
    for r in records:
        buckets[prefix_of(r["path"])][r["verdict"]] += 1
    n = len(records)
    print(f"== flat sweep: {n} cases, PASS {tot['PASS']}  FAIL {tot['FAIL']}  SKIP {tot['SKIP']}"
          f"  (pass rate of attempted: {tot['PASS']}/{tot['PASS'] + tot['FAIL']}) ==")
    print(f"{'bucket':16s} {'PASS':>5s} {'FAIL':>5s} {'SKIP':>5s}")
    for b, c in sorted(buckets.items(), key=lambda kv: -(kv[1]['PASS'] + kv[1]['FAIL'] + kv[1]['SKIP'])):
        if c["PASS"] + c["FAIL"] == 0 and not args.show_skips:
            continue
        print(f"{b:16s} {c['PASS']:5d} {c['FAIL']:5d} {c['SKIP']:5d}")

    skips = collections.Counter(r.get("skip_reason") for r in records if r["verdict"] == "SKIP")
    if skips:
        print("\nSKIP 原因：" + "；".join(f"{k} ×{v}" for k, v in skips.most_common(12)))

    fails = [r for r in records if r["verdict"] == "FAIL"]
    for r in fails[: args.show]:
        fd = r.get("first_diff") or {}
        print(f"\n  FAIL {os.path.basename(r['path'])}: {r.get('fail_summary', '')[:100]}")
        if fd:
            print(f"    期望 | {str(fd.get('expected'))[:140]}")
            print(f"    实际 | {str(fd.get('actual'))[:140]}")
    if len(fails) > args.show:
        print(f"\n（另有 {len(fails) - args.show} 个 FAIL 未展示）")
    live_diff = 0
    if args.oracle_live:
        passes = [r["path"] for r in records if r["verdict"] == "PASS"]

        def check(relpath):
            path = os.path.join(rt.TESTSUITE_ROOT, relpath.replace("/", os.sep))

            def run(cmd):
                try:
                    return subprocess.run(cmd, capture_output=True, text=True,
                                          timeout=args.timeout).stdout.replace("\r\n", "\n").strip()
                except Exception:
                    return None
            y = run([rt.YMC, path])
            o = run([args.omc, path])
            if o is None:
                return ("omc-timeout", relpath)
            if not (o.startswith("class") or o.startswith("function")):
                return ("omc-error", relpath)   # 1.25.0 自己报错（反向分歧，见账本）；函数发射用例 stdout 以 function 开头
            if y is None:
                return ("ymc-fail", relpath)
            return ("ok" if y == o else "diff", relpath)

        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            res = list(ex.map(check, passes))
        ok = [p for k, p in res if k == "ok"]
        diff = [p for k, p in res if k == "diff"]
        oe = [p for k, p in res if k in ("omc-error", "omc-timeout")]
        live_diff = len(diff)
        print(f"\n== oracle=live（实跑 omc 1.25.0）：{len(passes)} 个 PASS 用例 | 逐字节一致 {len(ok)}"
              f" | 不一致 {len(diff)} | omc 自身报错/超时 {len(oe)} ==")
        for p in diff:
            print(f"  ✗ 与 omc 1.25.0 不一致：{os.path.basename(p)}")
        for p in oe:
            print(f"  · omc 1.25.0 报错/超时（分歧账本）：{os.path.basename(p)}")
        if not diff:
            print("  ✓ 所有 PASS 用例与实跑 omc 1.25.0 逐字节一致")

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"total": dict(tot), "records": records}, f, ensure_ascii=False, indent=1)
    return 0 if (tot["FAIL"] == 0 and live_diff == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
