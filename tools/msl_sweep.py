#!/usr/bin/env python3
"""S3 退出条件（方案 S3 3g）：MSL 4.0.0 全部示例模型逐个压平，与实跑 omc 1.25.0 逐字节对拍，出通过率报告。

清单 docs/research/msl-examples.json：带 experiment 注解、非 partial 的 514 个模型，由 omc 生成——
  loadModel(Modelica, {"4.0.0"}); for c in getClassNames(Modelica, recursive = true, qualified = true) loop
    if isExperiment(c) and not isPartial(c) then print(typeNameString(c) + "\\n"); end if; end for;
两边各压平一个模型：
  ymc --load-model=Modelica --class=<模型>
  omc -i=<模型> "<库根>/Modelica 4.0.0+maint.om/package.mo"（库根同 ymc 的默认 MODELICAPATH：%APPDATA%/.openmodelica/libraries）
只比 stdout（omc 的 Notification / Warning 在 stderr）。omc 的结果按模型名缓存在 --oracle-cache（默认 build/msl-oracle），
omc 版本不变就不重跑；换版本时删掉缓存目录。

判定（同 nf_live_sweep）：PASS / FAIL / SKIP（ymc 退 66）/ BOTH-REJECT / ACCEPTS-INVALID / CRASH / OMC-TIMEOUT。

用法：
  python tools/msl_sweep.py                              # 全部 514 个
  python tools/msl_sweep.py --only Modelica.Blocks --show 20
  python tools/msl_sweep.py --json out.json --ymc build/msvc-asan/RelWithDebInfo/ymc.exe --timeout 180
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
LIST = os.path.join(REPO, "docs", "research", "msl-examples.json")
DEFAULT_LIB = os.path.join(os.environ.get("APPDATA", ""), ".openmodelica", "libraries", "Modelica 4.0.0+maint.om",
                           "package.mo")
_NOT_IMPL = re.compile(r"not implemented:\s*(.+)")


def run(cmd, timeout):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding="utf-8", errors="replace")
        return r.returncode, r.stdout.replace("\r\n", "\n").strip(), r.stderr
    except subprocess.TimeoutExpired:
        return None, "", ""


def oracle(name, args):
    """omc 的压平结果（缓存）：(ok, stdout)；超时 → (None, "")。"""
    cache = os.path.join(args.oracle_cache, name + ".txt")
    if os.path.exists(cache):
        with open(cache, encoding="utf-8") as f:
            text = f.read()
        # 空缓存 = 当年那次 omc 被打断（扫描进程被杀）：不可信，重跑。
        if text:
            return (None, "") if text == "<timeout>" else (text.startswith(("class", "function", "impure function")), text)
    code, out, _ = run([args.omc, "-i=" + name, args.lib], args.omc_timeout)
    # omc 正常失败也会在 stdout 打 "Error processing file"；什么都没打 = 被打断，不缓存（否则以后一直当 omc 拒绝）。
    if code is not None and not out:
        return None, ""
    os.makedirs(args.oracle_cache, exist_ok=True)
    with open(cache, "w", encoding="utf-8", newline="\n") as f:
        f.write("<timeout>" if code is None else out)
    if code is None:
        return None, ""
    return out.startswith(("class", "function", "impure function")), out


def check(name, args):
    rec = {"model": name}
    ycode, yout, yerr = run([args.ymc, "--load-model=Modelica", "--class=" + name], args.timeout)
    if ycode == 66:
        m = _NOT_IMPL.search(yerr + yout)
        rec.update(verdict="SKIP", reason=(m.group(1).strip()[:110] if m else "unspecified"))
        return rec
    ok, oout = oracle(name, args)
    if ycode != 0:
        both = ycode == 1 and ok is False and "YMC_ASSERT" not in (yerr + yout)
        rec.update(verdict="BOTH-REJECT" if both else "CRASH", reason=("timeout" if ycode is None else f"exit={ycode}"),
                   first=(yerr or yout).strip().split("\n")[0][:120])
        return rec
    if ok is None:
        rec.update(verdict="OMC-TIMEOUT")
        return rec
    if not ok:
        rec.update(verdict="ACCEPTS-INVALID", first=oout.split("\n")[0][:120])
        return rec
    if yout == oout:
        rec.update(verdict="PASS")
        return rec
    yl, ol = yout.split("\n"), oout.split("\n")
    i = next((k for k in range(min(len(yl), len(ol))) if yl[k] != ol[k]), min(len(yl), len(ol)))
    rec.update(verdict="FAIL", line=i + 1, expected=(ol[i] if i < len(ol) else "<EOF>")[:140],
               actual=(yl[i] if i < len(yl) else "<EOF>")[:140])
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join(REPO, "build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--omc", default="omc", help="omc 可执行文件（默认 PATH 上的安装版 1.25.0）")
    ap.add_argument("--lib", default=DEFAULT_LIB, help="omc 装载的 MSL package.mo")
    ap.add_argument("--oracle-cache", default=os.path.join(REPO, "build", "msl-oracle"))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--omc-timeout", type=int, default=120)
    ap.add_argument("--only", help="只跑这个前缀下的模型（如 Modelica.Blocks）")
    ap.add_argument("--show", type=int, default=20)
    ap.add_argument("--json")
    args = ap.parse_args()
    with open(LIST, encoding="utf-8") as f:
        names = json.load(f)
    if args.only:
        names = [n for n in names if n == args.only or n.startswith(args.only + ".")]
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        recs = list(ex.map(lambda n: check(n, args), names))

    tot = collections.Counter(r["verdict"] for r in recs)
    order = ["PASS", "FAIL", "ACCEPTS-INVALID", "SKIP", "BOTH-REJECT", "CRASH", "OMC-TIMEOUT"]
    print(f"== msl sweep: {len(recs)} 个示例模型 | " + " ".join(f"{k} {tot[k]}" for k in order) + " ==")
    by_pkg = collections.defaultdict(collections.Counter)
    for r in recs:
        by_pkg[".".join(r["model"].split(".")[:2])][r["verdict"]] += 1
    print(f"{'package':28s} " + " ".join(f"{k[:7]:>7s}" for k in order))
    for p, c in sorted(by_pkg.items(), key=lambda kv: -sum(kv[1].values())):
        print(f"{p:28s} " + " ".join(f"{c[k]:7d}" for k in order))
    skips = collections.Counter(re.sub(r" \[.*", "", r.get("reason", "")) for r in recs if r["verdict"] == "SKIP")
    if skips:
        print("\nSKIP 原因：")
        for k, v in skips.most_common(25):
            print(f"  {v:4d}  {k}")
    for r in [r for r in recs if r["verdict"] in ("FAIL", "ACCEPTS-INVALID", "CRASH")][: args.show]:
        print(f"\n  {r['verdict']} {r['model']}")
        if r["verdict"] == "FAIL":
            print(f"    期望 | {r['expected']}")
            print(f"    实际 | {r['actual']}")
        elif r.get("first"):
            print(f"    {r['first']}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"total": dict(tot), "records": recs}, f, ensure_ascii=False, indent=1)
    return 0 if tot["FAIL"] == 0 and tot["ACCEPTS-INVALID"] == 0 and tot["CRASH"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
