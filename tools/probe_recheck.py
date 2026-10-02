#!/usr/bin/env python3
"""探针回归：把 tests/probes/*.probes 里的每个模型同时交给 ymc 与实跑 omc 1.25.0，逐字节对拍 stdout。

探针是各加宽桶里为核实 omc 行为写的小模型（期望值不入库——每次都以实跑的 omc 为准）。语料扫描只能证明"语料里的写法
没坏"，探针集才是已核实行为的回归网：每桶收尾跑一遍，FAIL / ACCEPTS-INVALID / CRASH 必须为 0（KNOWN.txt 里登记的除外）。

判定（同 tools/flat_sweep.py 的口径，只比 stdout）：
  PASS             ymc 与 omc 输出逐字节一致
  SKIP             ymc 以非零退出声明未实现，omc 有结果（进度，不是 bug）
  BOTH-REJECT      两边都拒绝
  FAIL             都有输出但不一致（bug）
  ACCEPTS-INVALID  omc 报错而 ymc 出了结果（bug）
  CRASH            ymc 异常退出（bug）

包格式：`//// set: 名字 — 说明` 起头，随后每个 `//// model: 名字` 一段 Modelica 源码（omc / ymc 压平文件里最后一个类）。

用法：
  python tools/probe_recheck.py                       # 全部探针集
  python tools/probe_recheck.py ao-a1 v4-c            # 只跑这些集
  python tools/probe_recheck.py --show 5              # 每个出错模型打印前 5 行差异
  python tools/probe_recheck.py --keep out/probes     # 把拆出的 .mo 留在该目录（调试用）
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBES = os.path.join(REPO, "tests", "probes")
BAD = ("FAIL", "ACCEPTS-INVALID", "CRASH")


def load(path):
    """一个包 → [(模型名, 源码)]。"""
    out = []
    name, buf = None, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("//// model: "):
                if name:
                    out.append((name, "".join(buf)))
                name, buf = line[len("//// model: "):].strip(), []
            elif line.startswith("//// "):
                continue
            elif name:
                buf.append(line)
    if name:
        out.append((name, "".join(buf)))
    return out


def text(b):
    return b.decode("utf-8", errors="replace").replace("\r\n", "\n")


def run_one(job):
    d, name, args = job
    f = name + ".mo"
    try:
        o = text(subprocess.run([args.omc, f], cwd=d, capture_output=True, timeout=args.timeout).stdout)
    except subprocess.TimeoutExpired:
        return name, "OMC-TIMEOUT", "", ""
    try:
        y = subprocess.run([args.ymc, f], cwd=d, capture_output=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return name, "CRASH", o, "<ymc timeout>"
    yo = text(y.stdout)
    oerr = o.startswith("Error processing") or not o.strip()
    if y.returncode not in (0, 1, 66):
        return name, "CRASH", o, "<exit %d> %s" % (y.returncode, text(y.stderr).strip()[:200])
    if y.returncode != 0:
        return name, ("BOTH-REJECT" if oerr else "SKIP"), o, text(y.stderr).strip()
    if oerr:
        return name, "ACCEPTS-INVALID", o, yo
    return name, ("PASS" if yo == o else "FAIL"), o, yo


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sets", nargs="*", help="探针集名（缺省 = tests/probes 下全部）")
    ap.add_argument("--ymc", default=os.path.join(REPO, "build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--omc", default="omc", help="omc 可执行文件（默认 PATH 上的安装版 1.25.0）")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--show", type=int, default=0, help="每个出错模型打印前 N 行差异")
    ap.add_argument("--keep", help="拆出的 .mo 写到这个目录并保留")
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)

    known = {}
    kpath = os.path.join(PROBES, "KNOWN.txt")
    if os.path.exists(kpath):
        with open(kpath, encoding="utf-8") as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if line:
                    known[line.split()[0]] = True

    bundles = sorted(glob.glob(os.path.join(PROBES, "*.probes")))
    if args.sets:
        want = set(args.sets)
        bundles = [b for b in bundles if os.path.splitext(os.path.basename(b))[0] in want]
        missing = want - {os.path.splitext(os.path.basename(b))[0] for b in bundles}
        if missing:
            sys.exit("没有这些探针集：" + ", ".join(sorted(missing)))

    root = os.path.abspath(args.keep) if args.keep else tempfile.mkdtemp(prefix="ymc-probes-")
    total, bad = {}, []
    for b in bundles:
        sname = os.path.splitext(os.path.basename(b))[0]
        d = os.path.join(root, sname)
        os.makedirs(d, exist_ok=True)
        jobs = []
        for name, src in load(b):
            with open(os.path.join(d, name + ".mo"), "w", encoding="utf-8", newline="\n") as f:
                f.write(src)
            jobs.append((d, name, args))
        cnt = {}
        with ThreadPoolExecutor(args.jobs) as ex:
            for name, v, o, y in ex.map(run_one, jobs):
                if v in BAD and (sname + "/" + name) in known:
                    v = "KNOWN"
                cnt[v] = cnt.get(v, 0) + 1
                total[v] = total.get(v, 0) + 1
                if v in BAD:
                    bad.append((sname, name, v, o, y))
        print("%-18s %s" % (sname, "  ".join("%s %d" % kv for kv in sorted(cnt.items()))))
    print("== probes: %d 个模型 | %s ==" % (sum(total.values()), "  ".join("%s %d" % kv for kv in sorted(total.items()))))
    for sname, name, v, o, y in bad:
        print("  ✗ %s %s/%s" % (v, sname, name))
        if args.show:
            ol, yl = o.split("\n"), y.split("\n")
            shown = 0
            for i in range(max(len(ol), len(yl))):
                a = ol[i] if i < len(ol) else "<无>"
                c = yl[i] if i < len(yl) else "<无>"
                if a != c:
                    print("      omc| %s\n      ymc| %s" % (a, c))
                    shown += 1
                    if shown >= args.show:
                        break
    if not bad:
        print("  ✓ 无 FAIL / ACCEPTS-INVALID / CRASH")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
