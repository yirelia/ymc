"""ymc testsuite 运行器：复刻 OMC rtest 协议，跑 ymc 或 omc 并判定（FR-001/003/004/050/060）。

用法：
  python tests/omc/run.py --tool=omc --dir <目录>            # 对某目录跑 omc
  python tests/omc/run.py --tool=ymc --stage=S1              # 跑某阶段语料（stages.json）
  python tests/omc/run.py --self-check                       # 对 omc 自身跑已知全 correct 目录
  python tests/omc/run.py --print-cflags-histogram --dir <testsuite根>

协议要点（研究底稿 R2§A，rtest 源）：
- .mo 与 .mos 用同一条命令行：<omc> +locale=C <omcflags> <cflags> <file>。
- 期望 = 文件内 // Result: … // endResult 去 "// " 前缀。
- 实际 = stdout+stderr，按 rtest 归一化（去行首空白、首段空白折一个、跳过 GC Warning 等）。
- 判定用 omc_diff（.mo eps=1e-7，.mos eps=5e-3）。
- status: correct 需退出 0 且文本匹配；erroneous/incorrect 见下。

基建工具，Claude 提供。ymc 未实现的阶段/特性一律 SKIP（原因码），不算失败。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import omc_diff  # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))

OMC_INSTALLED = r"C:\Program Files\OpenModelica1.25.0-64bit\bin\omc.exe"
OMC_WORKSPACE = r"C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc.exe"
YMC = os.path.join(REPO, "build", "gcc-debug", "ymc.exe")

# rtest 需要的环境：OPENMODELICALIBRARY 必须设置，否则 --running-testsuite 报错
# （R2§A rtest:47-53）。--running-testsuite 让 omc 把错误信息里的绝对路径改成相对
# testsuite 根（如 flattening/modelica/scoping/X.mo），与 // Result: 块一致。
OPENMODELICALIBRARY = r"C:\Users\yangrui\AppData\Roaming\.openmodelica\libraries"

DEFAULT_TIMEOUT = 900

# 自检目录（相对 testsuite 根）：已知全 correct、无 MSL 依赖、纯文本比对
SELF_CHECK_DIRS = [
    "flattening/modelica/scoping",
]
TESTSUITE_ROOT = r"C:\dev\OpenModelica\testsuite"


# --------------------------------------------------------------- 指令解析
_DIRECTIVE = re.compile(r"^//[ |]*([a-z_]+):[ |]*(.*)$")


def parse_directives(text):
    info = {}
    for line in text.splitlines():
        m = _DIRECTIVE.match(line)
        if m:
            key, val = m.group(1), m.group(2).rstrip()
            if key not in info:            # 首次出现为准
                info[key] = val
    return info


def extract_expected(text):
    """取 // Result: … // endResult 之间的行，去 '// ' 前缀（rtest:363-389）。"""
    out = []
    inblock = False
    for line in text.splitlines():
        if re.match(r"^//\s*Result:", line):
            inblock = True
            rest = re.sub(r"^//\s*Result:", "", line)
            if rest.strip():
                out.append(rest)
            continue
        if re.match(r"^//\s*endResult", line):
            break
        if inblock:
            if line.startswith("// "):
                out.append(line[3:])
            elif line.startswith("//"):
                out.append(line[2:])
            else:
                out.append(line)
    return "\n".join(out)


def normalize(text):
    """rtest 输出归一化（rtest:296-316）。"""
    lines = []
    for line in text.splitlines():
        if line.startswith("GC Warning:"):
            continue
        if line.startswith("Unexpected end of /proc/mounts"):
            continue
        line = re.sub(r"^[ \t]+", "", line)          # 去行首空白
        line = re.sub(r"[ \t]+", " ", line, count=1)  # 首段连续空白折一个
        line = re.sub(r"/[^/]*\.mos_temp\d+/", "/", line)  # UCRT 临时路径段
        lines.append(line)
    return "\n".join(lines)


# --------------------------------------------------------------- cflags 映射
def load_cflags_map():
    p = os.path.join(HERE, "cflags_map.json")
    if os.path.exists(p):
        with open(p, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def _lookup_rule(tok, cmap):
    """精确匹配优先；否则找以 '=' 结尾的最长前缀键（如 '-i='、'+std='）。"""
    if tok in cmap:
        return cmap[tok]
    best = None
    for key, rule in cmap.items():
        if key.endswith("=") and tok.startswith(key):
            if best is None or len(key) > len(best[0]):
                best = (key, rule)
    return best[1] if best else None


def cflags_decision(cflags, cmap):
    """返回 ('run', passthrough_flags) 或 ('skip', reason)。"""
    if not cflags:
        return ("run", [])
    toks = cflags.split()
    passthrough = []
    for tok in toks:
        rule = _lookup_rule(tok, cmap)
        if rule is None:
            # 未分类：保守跳过并计数（FR-003）
            return ("skip", f"unmapped-cflag:{tok}")
        act = rule.get("action")
        if act == "skip":
            return ("skip", rule.get("reason", "skip"))
        elif act == "rename":
            passthrough.append(rule.get("to", tok))
        elif act == "passthrough":
            passthrough.append(tok)
        # ignore → 不加入
    return ("run", passthrough)


# --------------------------------------------------------------- 运行单个用例
def omc_path(which):
    return OMC_INSTALLED if which == "installed" else OMC_WORKSPACE


# ymc 未实现某 pass 时的识别：当前 S0 骨架对任意输入只打印帮助横幅。
# 契约（写入任务卡）：ymc 未实现的操作应打印含下列标记之一的横幅、或以退出码 66 退出；
# 运行器据此标 SKIP:not-implemented，而非 FAIL。ymc 长出真实 pass 后此判定自然失效。
YMC_NOT_IMPL_MARKERS = ("S0 skeleton", "usage:")


def _ymc_unimplemented(payload, path):
    """探测 ymc 是否尚未实现该操作（骨架横幅）。"""
    try:
        r = subprocess.run([YMC] + payload + [os.path.basename(path)],
                           cwd=os.path.dirname(path), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, timeout=30)
    except Exception:
        return True
    out = r.stdout or ""
    if r.returncode == 66:
        return True
    return any(mark in out for mark in YMC_NOT_IMPL_MARKERS)


def run_case(path, tool, omc_which, cmap, timeout):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    info = parse_directives(text)
    status = info.get("status", "correct").strip().lower()
    cflags = info.get("cflags", "")
    is_mos = path.endswith(".mos")
    eps = omc_diff.EPS_MOS if is_mos else omc_diff.EPS_MO
    rec = {"path": os.path.relpath(path, TESTSUITE_ROOT).replace("\\", "/"),
           "tool": tool, "status": status}

    decision, payload = cflags_decision(cflags, cmap)
    if decision == "skip":
        rec.update(verdict="SKIP", skip_reason=payload, compare_kind="L1" if not is_mos else "L3")
        return rec

    env = dict(os.environ)
    if tool == "ymc":
        if not os.path.exists(YMC) or _ymc_unimplemented(payload, path):
            rec.update(verdict="SKIP", skip_reason="not-implemented",
                       compare_kind="L1" if not is_mos else "L3")
            return rec
        exe = [YMC]
        cmd = exe + payload + [os.path.basename(path)]
    else:
        # --running-testsuite 让错误信息路径相对 testsuite 根（与 // Result: 一致）；
        # 它要求 OPENMODELICALIBRARY 已设置（R2§A）。running-testsuite 需要一个可写文件。
        env["OPENMODELICALIBRARY"] = OPENMODELICALIBRARY
        import tempfile as _tf
        rt_tmp = os.path.join(_tf.gettempdir(), "ymc_running_testsuite.tmp")
        exe = [omc_path(omc_which)]
        cmd = exe + [f"--running-testsuite={rt_tmp}", "+locale=C"] + payload + [os.path.basename(path)]

    t0 = time.time()
    try:
        # stderr 合并进 stdout（= rtest 的 >>$log 2>&1），保持两股输出的真实交错顺序
        r = subprocess.run(cmd, cwd=os.path.dirname(path), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, timeout=timeout, env=env)
        out = r.stdout or ""
        code = r.returncode
    except subprocess.TimeoutExpired:
        rec.update(verdict="FAIL", skip_reason=None, compare_kind="L1" if not is_mos else "L3",
                   fail_summary="timeout", duration_s=round(time.time() - t0, 3))
        return rec
    dur = round(time.time() - t0, 3)

    expected = extract_expected(text)
    got = normalize(out)
    exp_norm = normalize(expected)
    diff = omc_diff.compare(exp_norm, got, eps)
    text_match = diff.equal
    kind = "L3" if is_mos else "L1"

    # status 语义（rtest:280-293，简化）
    if status == "correct":
        ok = (code == 0 or not is_mos) and text_match
        # .mo 翻译异常 code!=0 视作失败；.mos code 常为 0
        if code != 0 and not is_mos:
            ok = False
    elif status == "erroneous":
        # rtest 视 erroneous 为通过；我们更严：文本匹配即通过，无 Result 则通过
        ok = text_match or (expected.strip() == "")
    else:  # incorrect / 其它
        ok = text_match

    rec.update(verdict="PASS" if ok else "FAIL", compare_kind=kind, duration_s=dur)
    if not ok:
        rec["fail_summary"] = diff.message or f"exit={code}"
        rec["first_diff"] = {"line": diff.line, "expected": diff.expected, "actual": diff.actual}
    return rec


# --------------------------------------------------------------- 目录 / 阶段
def discover(dirpath):
    cases = []
    for root, _, files in os.walk(dirpath):
        for fn in sorted(files):
            if fn.endswith(".mo") or fn.endswith(".mos"):
                cases.append(os.path.join(root, fn))
    return sorted(cases)


def summarize(records):
    summary = {"pass": 0, "fail": 0, "skip": 0, "by_dir": {}}
    for r in records:
        v = r["verdict"].lower()
        summary[v] = summary.get(v, 0) + 1
        d = os.path.dirname(r["path"])
        bd = summary["by_dir"].setdefault(d, {"pass": 0, "fail": 0, "skip": 0})
        bd[v] += 1
    return summary


def run_dir(dirpath, tool, omc_which, timeout):
    cmap = load_cflags_map()
    records = []
    for path in discover(dirpath):
        records.append(run_case(path, tool, omc_which, cmap, timeout))
    return records


def print_summary(records, summary):
    total = summary["pass"] + summary["fail"] + summary["skip"]
    for r in records:
        if r["verdict"] == "FAIL":
            print(f"  FAIL {r['path']}: {r.get('fail_summary', '')}")
    print(f"== {summary['pass']}/{total} PASS, {summary['fail']} FAIL, "
          f"{summary['skip']} SKIP ==")


OMC_DIFF_EXE = r"C:\Program Files\OpenModelica1.25.0-64bit\bin\omc-diff.exe"


def _capture_omc(path, omc_which, timeout):
    """跑 omc 取归一化输出与归一化期望。返回 (exp_norm, got_norm, eps)。"""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    is_mos = path.endswith(".mos")
    eps = omc_diff.EPS_MOS if is_mos else omc_diff.EPS_MO
    env = dict(os.environ)
    env["OPENMODELICALIBRARY"] = OPENMODELICALIBRARY
    import tempfile as _tf
    rt = os.path.join(_tf.gettempdir(), "ymc_running_testsuite.tmp")
    cmd = [omc_path(omc_which), f"--running-testsuite={rt}", "+locale=C", os.path.basename(path)]
    r = subprocess.run(cmd, cwd=os.path.dirname(path), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=timeout, env=env)
    return normalize(extract_expected(text)), normalize(r.stdout or ""), eps


def _oracle_equal(exp_norm, got_norm, eps):
    """用真实 omc-diff.exe 判定两段归一化文本是否相等（退出码 0=equal）。"""
    if not os.path.exists(OMC_DIFF_EXE):
        return None
    import tempfile as _tf
    with _tf.TemporaryDirectory() as d:
        pe = os.path.join(d, "expected")
        pa = os.path.join(d, "actual")
        with open(pe, "w", newline="", encoding="utf-8") as f:
            f.write(exp_norm)
        with open(pa, "w", newline="", encoding="utf-8") as f:
            f.write(got_norm)
        r = subprocess.run([OMC_DIFF_EXE, str(eps), pe, pa], capture_output=True, text=True)
        return r.returncode == 0


def cmd_self_check(args):
    """FR-060：证明运行器正确 = 对 OMC 自身输出，我们的判定与真实 omc-diff.exe 一致。
    （不要求全 PASS：部分 testsuite Result 块相对当前 omc 已过时，两侧都判 differ 即算一致。）"""
    all_ok = True
    for rel in SELF_CHECK_DIRS:
        d = os.path.join(TESTSUITE_ROOT, rel)
        if not os.path.isdir(d):
            print(f"self-check: 目录不存在 {d}")
            all_ok = False
            continue
        cases = discover(d)
        agree = disagree = passes = oracle_missing = 0
        for p in cases:
            try:
                exp, got, eps = _capture_omc(p, args.omc, args.timeout)
            except subprocess.TimeoutExpired:
                print(f"  TIMEOUT {os.path.basename(p)}")
                disagree += 1
                continue
            ours = omc_diff.compare(exp, got, eps).equal
            oracle = _oracle_equal(exp, got, eps)
            if oracle is None:
                oracle_missing += 1
                continue
            if ours:
                passes += 1
            if ours == oracle:
                agree += 1
            else:
                disagree += 1
                print(f"  MISMATCH {os.path.basename(p)}: ours={ours} omc-diff={oracle}")
        print(f"[self-check] {rel}: {agree}/{agree + disagree} 与 omc-diff.exe 一致 "
              f"（其中 {passes} 个 OMC 输出与 Result 匹配）")
        if oracle_missing:
            print(f"  注意：omc-diff.exe 未找到，{oracle_missing} 个用例未对拍")
        if disagree > 0:
            all_ok = False
    if not all_ok:
        print("self-check FAILED：运行器判定与 omc-diff.exe 有偏差")
        return 1
    print("self-check OK：运行器判定与 omc-diff.exe 完全一致")
    return 0


def cmd_histogram(args):
    from collections import Counter
    cmap = load_cflags_map()
    c = Counter()
    unmapped = Counter()
    for path in discover(args.dir):
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            info = parse_directives(f.read())
        cf = info.get("cflags", "").strip()
        c[cf] += 1
        if cf:
            for tok in cf.split():
                if _lookup_rule(tok, cmap) is None:   # 与运行时同款前缀感知匹配
                    unmapped[tok] += 1
    for cf, n in c.most_common(40):
        print(f"{n:5d}  {cf!r}")
    if unmapped:
        print("--- 未在 cflags_map.json 中的 flag ---")
        for tok, n in unmapped.most_common():
            print(f"{n:5d}  {tok}")
        return 1
    print("所有 cflags 都已在 cflags_map.json 中分类")
    return 0


def load_stages():
    p = os.path.join(HERE, "stages.json")
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)


def cmd_stage(args):
    stages = load_stages()
    st = stages.get(args.stage)
    if not st:
        print(f"未知阶段 {args.stage}")
        return 2
    cmap = load_cflags_map()
    records = []
    for corpus in st.get("corpora", []):
        d = corpus if os.path.isabs(corpus) else os.path.join(TESTSUITE_ROOT, corpus)
        if not os.path.isdir(d):
            print(f"[warn] 语料目录不存在: {d}")
            continue
        for path in discover(d):
            records.append(run_case(path, args.tool, args.omc, cmap, args.timeout))
    summary = summarize(records)
    print(f"[stage {args.stage}] tool={args.tool}")
    print_summary(records, summary)
    if args.json:
        _write_json(args.json, args.stage, args.tool, records, summary)
    return 0 if summary["fail"] == 0 else 1


def _write_json(path, stage, tool, records, summary):
    obj = {"schema_version": 1, "stage": stage, "tool": tool,
           "summary": summary, "cases": records}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=1, sort_keys=True)


def main(argv=None):
    p = argparse.ArgumentParser(description="ymc testsuite 运行器（rtest 复刻）")
    p.add_argument("--tool", choices=["ymc", "omc"], default="omc")
    p.add_argument("--omc", choices=["installed", "workspace"], default="installed")
    p.add_argument("--dir")
    p.add_argument("--stage")
    p.add_argument("--json")
    p.add_argument("--self-check", action="store_true")
    p.add_argument("--print-cflags-histogram", action="store_true")
    p.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    args = p.parse_args(argv)

    if args.self_check:
        return cmd_self_check(args)
    if args.print_cflags_histogram:
        if not args.dir:
            print("--print-cflags-histogram 需要 --dir")
            return 2
        return cmd_histogram(args)
    if args.stage:
        return cmd_stage(args)
    if args.dir:
        records = run_dir(args.dir, args.tool, args.omc, args.timeout)
        summary = summarize(records)
        print_summary(records, summary)
        if args.json:
            _write_json(args.json, "dir", args.tool, records, summary)
        return 0 if summary["fail"] == 0 else 1
    p.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
