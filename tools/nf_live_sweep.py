#!/usr/bin/env python3
"""旧前端用例的新前端对拍：去掉 `-d=-newInst` 注解后，ymc 与实跑 omc 1.25.0（新前端）逐字节比 stdout。

flattening/modelica 下约 1100 个 .mo 用注解 `annotation(__OpenModelica_commandLineOptions="-d=-newInst");` 钉在 OMC
旧前端上，文件里的 `// Result:` 是旧前端输出，按方案 §1.2 不做 L1 文本比对（flat_sweep 记 SKIP:old-frontend）。
但模型本身是好语料：去掉注解后 1.25.0 走新前端（默认），它的输出就是 ymc 的权威 oracle（CLAUDE.md：一律以 1.25.0
实测为准）。只取注解值**恰为** "-d=-newInst" 的文件（夹带其它选项的会改语义，不取）。注解所在行若只有这条注解则清成
空行，保持行号不变。

判定（ymc 与 omc 都跑去注解后的副本；只比 stdout）：
  PASS            stdout 逐字节一致
  FAIL            ymc 出了结果但与 omc 不同（bug；`omc-warn` 标记 omc 带告警行——ymc 不产告警，应 SKIP）
  ACCEPTS-INVALID omc 报错，ymc 却出了结果（应拒绝 → bug）
  SKIP            ymc 退出码 66（未实现）
  BOTH-REJECT     ymc 自己报错（退出码 1）且 omc 也报错（文案对拍另算，不计 bug）
  CRASH           ymc 断言 / 其它非零退出 / 超时，或 ymc 报错而 omc 能过
  OMC-TIMEOUT     omc 超时（不计）

`--all` 再把**没有**命令行选项注解的 .mo 也收进来（原样跑）——任何一个装着模型的目录都能当对拍语料，比如
simulation/modelica（那里的 .mo 由 .mos 驱动仿真，文件本身没有 `// Result:`）。`omc X.mo` 压平的是文件里最后一个类，
ymc 同此；要靠别的库（MSL）的模型两边都拒绝，记 SKIP / BOTH-REJECT。

用法：
  python tools/nf_live_sweep.py                         # 默认 flattening/modelica 全部
  python tools/nf_live_sweep.py --dir <目录> --json out.json --show 20
  python tools/nf_live_sweep.py --all --dir <testsuite>/simulation/modelica
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tests", "omc"))
import run as rt  # noqa: E402  （tests/omc/run.py）

DEFAULT_DIR = os.path.join(rt.TESTSUITE_ROOT, "flattening", "modelica")
_ANN = r'annotation\s*\(\s*__OpenModelica_commandLineOptions\s*=\s*"-d=-newInst"\s*\)'
_ONLY_LINE = re.compile(r"^\s*" + _ANN + r"\s*;\s*$")
_INLINE = re.compile(r"\s*" + _ANN)
_ANY_OPTS = re.compile(r'__OpenModelica_commandLineOptions\s*=\s*"([^"]*)"')
_NOT_IMPL = re.compile(r"not implemented:\s*(.+)")


def strip_annotation(text, take_plain=False):
    """去掉 `-d=-newInst` 注解；文件里有其它命令行选项注解 → None（不取）。没有注解的：take_plain 时原样取，否则不取。"""
    opts = _ANY_OPTS.findall(text)
    if not opts:
        return text if take_plain and "__OpenModelica_commandLineOptions" not in text else None
    if any(o != "-d=-newInst" for o in opts):
        return None
    out = []
    for line in text.split("\n"):
        out.append("" if _ONLY_LINE.match(line) else _INLINE.sub("", line))
    stripped = "\n".join(out)
    return None if "__OpenModelica_commandLineOptions" in stripped else stripped


def run(cmd, cwd, timeout):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
        return r.returncode, r.stdout.replace("\r\n", "\n").strip(), r.stderr
    except subprocess.TimeoutExpired:
        return None, "", ""


def check(path, tmp, args):
    rel = os.path.relpath(path, rt.TESTSUITE_ROOT).replace("\\", "/")
    with open(path, encoding="utf-8", errors="replace") as f:
        text = f.read()
    rec = {"path": rel, "status": rt.parse_directives(text).get("status", "correct").strip().lower()}
    stripped = strip_annotation(text, args.all)
    if stripped is None:
        return None   # 不是纯 -d=-newInst 的旧前端用例（--all 时：带其它命令行选项的）
    dst = os.path.join(tmp, rel)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(stripped)
    ycode, yout, yerr = run([rt.YMC, dst], os.path.dirname(dst), args.timeout)
    if ycode == rt.YMC_NOT_IMPL_EXIT:
        m = _NOT_IMPL.search(yerr + yout)
        rec.update(verdict="SKIP", reason=(m.group(1).strip()[:60] if m else "unspecified"))
        return rec
    ocode, oout, _ = run([args.omc, dst], os.path.dirname(dst), args.omc_timeout)
    omc_ok = ocode is not None and oout.startswith(("class", "function", "impure function"))
    if ycode != 0:
        # ymc 自己报错（解析 / 语义错误，退出码 1）且 omc 也报错 = 都拒绝（文案对拍属错误输出桶）；ymc 断言 / 崩溃 /
        # 超时，或 omc 能过 = CRASH。
        both = ycode == 1 and ocode is not None and not omc_ok and "YMC_ASSERT" not in (yerr + yout)
        rec.update(verdict="BOTH-REJECT" if both else "CRASH",
                   reason=("timeout" if ycode is None else f"exit={ycode}"),
                   first=(yerr or yout).strip().split("\n")[0][:120])
        return rec
    if ocode is None:
        rec.update(verdict="OMC-TIMEOUT")
        return rec
    if not omc_ok:
        rec.update(verdict="ACCEPTS-INVALID", first=oout.split("\n")[0][:120])
        return rec
    if yout == oout:
        rec.update(verdict="PASS")
        return rec
    yl, ol = yout.split("\n"), oout.split("\n")
    i = next((k for k in range(min(len(yl), len(ol))) if yl[k] != ol[k]), min(len(yl), len(ol)))
    rec.update(verdict="FAIL", omc_warn="Warning:" in oout, line=i + 1,
               expected=(ol[i] if i < len(ol) else "<EOF>")[:140], actual=(yl[i] if i < len(yl) else "<EOF>")[:140])
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default=DEFAULT_DIR)
    ap.add_argument("--ymc", default=None)
    ap.add_argument("--omc", default="omc", help="omc 可执行文件（默认 PATH 上的安装版 1.25.0）")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--omc-timeout", type=int, default=60)
    ap.add_argument("--show", type=int, default=20)
    ap.add_argument("--json")
    ap.add_argument("--all", action="store_true", help="也取没有命令行选项注解的 .mo（原样跑）")
    args = ap.parse_args()
    if args.ymc:
        rt.YMC = os.path.abspath(args.ymc)

    cases = [p for p in rt.discover(args.dir) if p.endswith(".mo")]
    tmp = tempfile.mkdtemp(prefix="ymc_nf_live_")
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            records = [r for r in ex.map(lambda p: check(p, tmp, args), cases) if r is not None]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    tot = collections.Counter(r["verdict"] for r in records)
    what = "个模型（--all：无注解的原样 + 去 -d=-newInst 的）" if args.all else "个旧前端用例（去 -d=-newInst）"
    print(f"== nf-live: {len(records)} {what}| " +
          " ".join(f"{k} {tot[k]}" for k in ("PASS", "FAIL", "ACCEPTS-INVALID", "SKIP", "BOTH-REJECT", "CRASH", "OMC-TIMEOUT")) + " ==")
    by_dir = collections.defaultdict(collections.Counter)
    for r in records:
        parts = r["path"].split("/")   # flattening/modelica/<目录>/X.mo → <目录>；层数不够的取文件所在目录
        by_dir[parts[2] if len(parts) > 3 else parts[-2] if len(parts) > 1 else "."][r["verdict"]] += 1
    print(f"{'dir':24s} {'PASS':>5s} {'FAIL':>5s} {'ACC-INV':>7s} {'SKIP':>5s} {'CRASH':>5s}")
    for d, c in sorted(by_dir.items(), key=lambda kv: -sum(kv[1].values())):
        print(f"{d:24s} {c['PASS']:5d} {c['FAIL']:5d} {c['ACCEPTS-INVALID']:7d} {c['SKIP']:5d} {c['CRASH']:5d}")
    skips = collections.Counter(r["reason"] for r in records if r["verdict"] == "SKIP")
    if skips:
        print("\nSKIP 原因：" + "；".join(f"{k} ×{v}" for k, v in skips.most_common(15)))
    bad = [r for r in records if r["verdict"] in ("FAIL", "ACCEPTS-INVALID", "CRASH")]
    for r in bad[: args.show]:
        tag = r["verdict"] + (" (omc-warn)" if r.get("omc_warn") else "")
        print(f"\n  {tag} {r['path']}")
        if r["verdict"] == "FAIL":
            print(f"    omc | {r['expected']}\n    ymc | {r['actual']}")
        else:
            print(f"    {r.get('first', r.get('reason', ''))}")
    if len(bad) > args.show:
        print(f"\n（另有 {len(bad) - args.show} 个未展示）")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"total": dict(tot), "records": records}, f, ensure_ascii=False, indent=1)
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
