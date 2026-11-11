#!/usr/bin/env python3
"""S5a 门：`ymc --dump-after=<段>` 与 omc 1.25.0 新后端的 `BackendDAE: (<段>)` 逐字节比（ADR-008 §6）。--stage 选段，缺省 lower = (After Lowering)。

参考：读 <refs>/sections/<段>/<id>.txt（tools/bdae_refgen.py run --keep-sections / sections 生成），语料 a 没有时退回 <refs>/<id>/out.txt。
--probes 不读参考，现跑 omc（命令同 bdae_refgen.FLAGS）。参考文本是唯一的准入依据，所以判定不依赖 ymc 自己的看法。

判定：
  PASS            字节相同
  FAIL            ymc 打了这一段，字节不同（bug）
  SKIP            ymc 退 66，reason = 未实现的原因
  CRASH           ymc 断言 / 超时 / 其它退出码（含 exit 1：omc 能过的模型 ymc 拒绝了）
  NOT-ADMITTED    参考不在门内：
                    no-section   omc 没打这一段（前端报错 / lower 失败）
                    A2'          (After Lowering) 有数组型变量、Record 桶非空、或有 [ARRY] [FOR-] [RECD] [TUPL]
                    A5           表达式里有数组型子表达式或推导式：NB 路径不展开（NFInst.mo:324-333），默认路径展开了
                    A1           该模型压平和 omc 不一致（语料 a 的 a1_a.jsonl）
                    <上一段>:<判定>  --stage 不是 lower 时：该模型在上一段（NB 运行序，lower → bindings → functionalias → earlyinline → simplify1 → alias）不是 PASS，
                                 这一段的输入就没验证过，不比。判定是 NOT-ADMITTED 时带上原因，所以会一层层套（bindings:NOT-ADMITTED:lower:FAIL）
                  NOT-ADMITTED 的模型 ymc 照样跑，只看是否崩溃（崩溃 = CRASH）；打出来的文本不比，no-section 而 ymc 退 0 的单列一行。
                  A2' / A5 按这一段自己的文本判（--stage bindings 时绑定方程形如数组、记录的归 A2'）；A3 不要求，这一段在手就行。

A5 按文本判（表达式里去掉字符串字面量后）：`{` `[`（数组构造、矩阵、下标）、` for `（推导式）、`$i<数字>`（绑定迭代量）、整型范围、
点号运算符、数组型内建调用（A5_CALLS）。两个位置例外，都不是路径差异（ADR-008 §6 A5 补充）：
  [WHEN] 和 [ALGO] 的 when / elsewhen 条件顶层的数组字面量 `when {a, b} then`（含 split 拼出来的 `not {a, b} and c`）：去掉这对括号，元素照旧查。
  [ALGO] 的 for 语句头 `for i in <范围> loop`：语句不展开（NFFlatten.flattenStatement），范围里的 `:` 不算，数字字面量的数组不算；范围里别的东西照旧查。

JSON 与 tools/nf_live_sweep.py 同形（{"total", "records": [{path, status, verdict, …}]}），两次结果可用 vdiff 逐用例比。

用法：
  python -I tools/bdae_sweep.py --corpus a --ymc <ymc.exe> [--jobs 8] [--json out.json] [--show 10]
  python -I tools/bdae_sweep.py --corpus b --ymc <ymc.exe>
  python -I tools/bdae_sweep.py --cases <cases.json> [--a1 <a1.jsonl>] --ymc <ymc.exe>
  python -I tools/bdae_sweep.py --probes [集名…] --ymc <ymc.exe>     # tests/probes/bdae/*.probes，现跑 omc
  以上都可加 --stage lower|bindings|functionalias|earlyinline|simplify1|alias

基建工具，Claude 提供。
"""
from __future__ import annotations

import argparse
import collections
import difflib
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bdae_refgen as rg  # noqa: E402
import probe_recheck as pr  # noqa: E402
import structcompare as sc  # noqa: E402

REPO = os.path.dirname(HERE)
PROBES = os.path.join(REPO, "tests", "probes", "bdae")
CENSUS = os.path.join(rg.REFS, "census")
_NOT_IMPL = re.compile(r"not implemented:\s*(.+)")
_STR = re.compile(r'"(?:[^"\\]|\\.)*"', re.S)
_TAG = re.compile(r"\[[-A-Z]{4}\]")        # 多行方程续行里的 [----]、if 体里的 [SCAL]
A5_CALLS = ("fill", "zeros", "ones", "identity", "diagonal", "cat", "transpose", "linspace", "array", "symmetric", "promote",
            "skew", "cross", "outerProduct", "vector", "matrix", "scalar", "size", "ndims", "sum", "product", "min", "max")
_A5 = [("array-literal", re.compile(r"\{")), ("subscript", re.compile(r"\[")), ("reduction", re.compile(r" for ")),
       ("binding-iterator", re.compile(r"\$i\d")), ("elementwise-op", re.compile(r" \.[-+*/^] ")),
       ("range", re.compile(r"(?<![\w.])-?\w[\w.]*:\w"))]
_A5_CALL = re.compile(r"(?<![\w.$'])(" + "|".join(A5_CALLS) + r")\(")
_WHEN_HEAD = re.compile(r"^((?:else)?when )(.*)( then)$", re.M)
_FOR_HEAD = re.compile(r"^ *for (?:'[^']*'|\w+) in (.*) loop$", re.M)
_NUM_ARRAY = re.compile(r"^\{[-+0-9., e]*\}$")
_TOP_L = re.compile(r"(?:^|(?:not|and) )\(*$")
_TOP_R = re.compile(r"\)*(?: and |$)")
# --stage 的值 = ymc --dump-after 的值 → omc 的段名。按 NB 的运行序排（NBackendDAE.mo:268-278 v1.25.0），stage_why 靠这个序找上一段。
# Simplify 在 NB 里跑两次（:272、:274），段名带序号，参考目录跟着段名走（sections/simplify_1、simplify_2，structcompare.stage_dir）。
STAGES = {"lower": "After Lowering", "bindings": "Bindings", "functionalias": "FunctionAlias", "earlyinline": "Early Inline",
          "simplify1": "Simplify 1", "alias": "Alias"}
ORDER = list(STAGES)


def body_of(section):
    """段文本去掉 headline_1 那五行。"""
    return section.split("\n", 5)[5]


def _strip_top_braces(cond):
    """去掉 when 条件顶层的数组字面量外壳：`{` 不在调用的括号里、前面是条件开头或 `not ` / `and `，配对的 `}` 后面是结尾或 ` and `。
    别的位置（`sum({a, b}) > 1`、`{a, b}[1]`）不动，留给 A5 判。"""
    drop, braces, calls = set(), [], []
    for i, ch in enumerate(cond):
        if ch == "(":
            calls.append(i > 0 and (cond[i - 1].isalnum() or cond[i - 1] in "_'"))
        elif ch == ")":
            calls.pop()
        elif ch == "{":
            top = not braces and not any(calls) and _TOP_L.search(cond[:i])
            braces.append(i if top else None)
        elif ch == "}":
            j = braces.pop()
            if j is not None and _TOP_R.match(cond[i + 1:]):
                drop.update((i, j))
    return "".join(c for k, c in enumerate(cond) if k not in drop)


def _for_range(m):
    """for 语句头换成范围里还要查的部分。"""
    r = m.group(1)
    return "" if _NUM_ARRAY.match(r) else r.replace(":", " ")


def admission(section):
    """(通过?, 原因)。原因是 A2':xxx / A5:xxx。"""
    if section is None:
        return False, "no-section"
    b = body_of(section)
    s = rg.lowering_shape(b)
    if s["array_vars"]:
        return False, "A2':array-variable"
    if s["record_vars"]:
        return False, "A2':record-variable"
    if s["array_eqs"]:
        return False, "A2':array-equation"
    if s["record_eqs"]:
        return False, "A2':record-equation"
    v, e = sc.parse_var_data(b), sc.parse_eq_data(b)
    texts = [i["decl"] for k, bk in v["buckets"].items() if k != "Artificial" for i in bk["items"]]
    for bk in e["buckets"].values():
        for i in bk["items"]:
            t = _STR.sub('""', i["text"])
            if i["tag"] in ("[WHEN]", "[ALGO]"):
                t = _WHEN_HEAD.sub(lambda m: m.group(1) + _strip_top_braces(m.group(2)) + m.group(3), t)
            if i["tag"] == "[ALGO]":
                t = _FOR_HEAD.sub(_for_range, t)
            texts.append(t)
    for t in texts:
        t = _TAG.sub("", _STR.sub('""', t))
        for name, pat in _A5:
            if pat.search(t):
                return False, "A5:" + name
        m = _A5_CALL.search(t)
        # min / max / sum / product 的标量形式：实参里没有数组痕迹（上面已查）就是普通标量调用，留在门内。
        if m and m.group(1) not in ("min", "max", "sum", "product"):
            return False, "A5:call-" + m.group(1)
    return True, ""


def ref_section(c, stage):
    # 第二处是 --keep-sections 之前的老位置：语料 b 的 sections/ 重跑完之前，lower 段还读它。
    for p in (rg.section_path(rg.REFS, STAGES[stage], c["id"]),
              os.path.join(rg.REFS, "after_lowering", rg.safe_id(c["id"]) + ".txt") if stage == "lower" else ""):
        if os.path.exists(p):
            with open(p, encoding="utf-8", newline="\n") as f:
                return f.read()
    p = os.path.join(rg.REFS, rg.safe_id(c["id"]), "out.txt")
    if os.path.exists(p):
        return sc.stage_section_texts(rg._read(p)).get(STAGES[stage])
    return None


def run_ymc(ymc, text, cls, d, timeout, stage):
    """同 bdae_refgen.a1_one 的选类办法。返回 (rc 或 None, stdout, stderr)。"""
    dump = "--dump-after=" + stage
    cmd = [ymc, dump, "model.mo"]
    if "." in cls:           # 限定名目标：ymc 只有 --load-model 路线能选类
        top = cls.split(".")[0]
        with open(os.path.join(d, top + ".mo"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        cmd = [ymc, dump, f"--modelica-path={d}", f"--load-model={top}", f"--class={cls}"]
    elif cls:
        text = rg.move_class_last(text, cls) or text
    with open(os.path.join(d, "model.mo"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    try:
        r = subprocess.run(cmd, cwd=d, capture_output=True, timeout=timeout, stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        return None, "", ""
    return r.returncode, r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")


def judge(rec, section, why_not, rc, out, err):
    """rec 填 verdict。why_not 非空 = 参考不在门内。"""
    rec["ymc_rc"] = rc
    if rc not in (0, 66, 1) or (rc == 1 and not why_not):
        first = (err or out).strip().split("\n")[0][:160]
        rec.update(verdict="CRASH", reason="timeout" if rc is None else f"exit={rc}", first=first)
    elif why_not:
        rec.update(verdict="NOT-ADMITTED", reason=why_not)
    elif rc == 66:
        m = _NOT_IMPL.search(err + out)
        rec.update(verdict="SKIP", reason=m.group(1).strip()[:110] if m else "unspecified")
    elif out == section:
        rec.update(verdict="PASS")
    else:
        sl, ol = section.split("\n"), out.split("\n")
        i = next((k for k in range(min(len(sl), len(ol))) if sl[k] != ol[k]), min(len(sl), len(ol)))
        rec.update(verdict="FAIL", line=i + 1, expected=(sl[i] if i < len(sl) else "<EOF>")[:200],
                   actual=(ol[i] if i < len(ol) else "<EOF>")[:200])
    return rec


def stage_why(stage, sec, why_lower, text, cls, d, a):
    """这一段的 NOT-ADMITTED 原因，在门内返回 ""。sec(段) 给参考段文本，why_lower 是 lower 段自己的准入原因。
    不是 lower 时前提是上一段（STAGES 的序 = NB 的运行序）PASS，上一段又有它自己的前提，递归下去：
    上一段不是 PASS 就判 <上一段>:<判定>，NOT-ADMITTED 再带上原因（`bindings:NOT-ADMITTED:lower:FAIL`）。"""
    if stage == "lower":
        return why_lower
    prev = ORDER[ORDER.index(stage) - 1]
    pwhy = stage_why(prev, sec, why_lower, text, cls, d, a)
    rc, out, err = run_ymc(a.ymc, text, cls, d, a.timeout, prev)
    r = judge({}, sec(prev), pwhy, rc, out, err)
    if r["verdict"] != "PASS":
        return prev + ":" + r["verdict"] + (":" + r["reason"] if r["verdict"] == "NOT-ADMITTED" else "")
    return admission(sec(stage))[1]


def one_case(c, a, a1, omc_rc=None):
    """omc_rc：参考生成时 omc 的退出码（census 的 sig_<语料>.jsonl）。崩溃时（Windows 0xC0000005）末段被 complete_sections 丢掉，之后的段本来
    就没有，这一段缺了不是 omc 不打，同 one_probe 记成 omc-exit=…（语料 b 的 ClassRestriction1：Alias 段的头已经印出、整段在丢掉的缓冲里）。"""
    rec = {"path": c["id"], "status": "correct"}
    lsec = ref_section(c, "lower")
    _, why_l = admission(lsec)
    if not why_l and a1 is not None and not a1.get(c["id"], {}).get("a1"):
        why_l = "A1:" + a1.get(c["id"], {}).get("why", "?")
    text = rg.case_text(c)
    if text is None:
        return dict(rec, verdict="NOT-ADMITTED", reason="annotation")
    cls = c.get("cls", "")
    d = tempfile.mkdtemp(prefix="bs_", dir=a.tmp)
    try:
        sec = lambda s: lsec if s == "lower" else ref_section(c, s)  # noqa: E731
        section = sec(a.stage)
        why = stage_why(a.stage, sec, why_l, text, cls, d, a)
        rc, out, err = run_ymc(a.ymc, text, cls, d, a.timeout, a.stage)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    rec = judge(rec, section, why, rc, out, err)
    if rec["verdict"] == "NOT-ADMITTED" and section is None and omc_rc not in (None, 0, 1):
        rec["reason"] = f"omc-exit={omc_rc}"
    return rec


def one_probe(job, a):
    sname, name, src = job
    rec = {"path": f"bdae/{sname}/{name}", "status": "correct"}
    d = tempfile.mkdtemp(prefix="bp_", dir=a.tmp)
    try:
        rc, status, _ = rg.run_omc([rg.OMC] + rg.FLAGS + ["model.mo"], _write(d, src), a.omc_timeout, 512 * 1024 * 1024)
        if status != "done":
            return dict(rec, verdict="NOT-ADMITTED", reason="omc-" + status)
        texts = rg.complete_sections(rg._read(os.path.join(d, "out.txt")), rc, status)
        sec = lambda s: texts.get(STAGES[s])  # noqa: E731
        section = sec(a.stage)
        why = stage_why(a.stage, sec, admission(sec("lower"))[1], src, "", d, a)
        orc = rc
        rc, out, err = run_ymc(a.ymc, src, "", d, a.timeout, a.stage)
        rec = judge(rec, section, why, rc, out, err)
        # omc 在后面的模块崩溃（Windows 上退出码 3221225477 = 0xC0000005 访问违例）时 stdout 缓冲断在中间，complete_sections 丢了末段，之后的段本来就没有（LbnW008 断在 Simplify 1，
        # 加 failtrace 的完整段与 ymc 相同）。这一段缺了不是 omc 不打，单列，不算"omc 没有这一段、ymc 打了"。
        if rec["verdict"] == "NOT-ADMITTED" and section is None and orc not in (0, 1):
            rec["reason"] = f"omc-exit={orc}"
        if rec["verdict"] == "FAIL" and a.keep_fail:
            os.makedirs(a.keep_fail, exist_ok=True)
            with open(os.path.join(a.keep_fail, name + ".diff"), "w", encoding="utf-8") as f:
                f.write("\n".join(difflib.unified_diff(section.split("\n"), out.split("\n"), "omc", "ymc", lineterm="")))
        return rec
    finally:
        shutil.rmtree(d, ignore_errors=True)


def _write(d, src):
    with open(os.path.join(d, "model.mo"), "w", encoding="utf-8", newline="\n") as f:
        f.write(src)
    return d


def report(records, a, what):
    tot = collections.Counter(r["verdict"] for r in records)
    stage = "" if a.stage == "lower" else " " + a.stage
    print(f"== bdae-sweep {what}{stage}: {len(records)} | " +
          " ".join(f"{k} {tot[k]}" for k in ("PASS", "FAIL", "SKIP", "CRASH", "NOT-ADMITTED")) + " ==")
    for label, verdict in (("SKIP 原因", "SKIP"), ("NOT-ADMITTED 原因", "NOT-ADMITTED")):
        c = collections.Counter(r["reason"] for r in records if r["verdict"] == verdict)
        if c:
            print(f"\n{label}：")
            for k, v in c.most_common():
                print(f"  {v:5d}  {k}")
    # 不算 FAIL：omc 没这段可能只是 NB 自己 lower 失败。但 ymc 打了文本，得看得见（语义上可能是 ymc 漏了 omc 的报错）。
    leak = [r["path"] for r in records if r.get("reason", "").endswith("no-section") and r.get("ymc_rc") == 0]
    if leak:
        print(f"\nomc 没有这一段、ymc 打了：{len(leak)}  {leak[: a.show]}")
    bad = [r for r in records if r["verdict"] in ("FAIL", "CRASH")]
    for r in bad[: a.show]:
        print(f"\n  {r['verdict']} {r['path']}")
        if r["verdict"] == "FAIL":
            print(f"    :{r['line']}\n    omc | {r['expected']}\n    ymc | {r['actual']}")
        else:
            print(f"    {r['reason']}  {r.get('first', '')}")
    if len(bad) > a.show:
        print(f"\n（另有 {len(bad) - a.show} 个未展示）")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"total": dict(tot), "records": records}, f, ensure_ascii=False, indent=1)
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sets", nargs="*", help="--probes 时：探针集名（缺省 = tests/probes/bdae 下全部）")
    ap.add_argument("--corpus", choices=["a", "b"])
    ap.add_argument("--cases")
    ap.add_argument("--a1")
    ap.add_argument("--probes", action="store_true")
    ap.add_argument("--stage", choices=sorted(STAGES), default="lower")
    ap.add_argument("--ymc", default=os.path.join(REPO, "build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--omc-timeout", type=int, default=120)
    ap.add_argument("--show", type=int, default=10)
    ap.add_argument("--json")
    ap.add_argument("--keep-fail", help="--probes：把 FAIL 的 unified diff 写到这个目录")
    ap.add_argument("--tmp", default=os.path.join(tempfile.gettempdir(), "ymc_bdae_sweep"))
    a = ap.parse_args()
    a.ymc = os.path.abspath(a.ymc)
    os.makedirs(a.tmp, exist_ok=True)

    if a.probes:
        bundles = sorted(glob.glob(os.path.join(PROBES, "*.probes")))
        if a.sets:
            bundles = [b for b in bundles if os.path.splitext(os.path.basename(b))[0] in set(a.sets)]
        jobs = [(os.path.splitext(os.path.basename(b))[0], n, s) for b in bundles for n, s in pr.load(b)]
        with ThreadPoolExecutor(a.jobs) as ex:
            return report(list(ex.map(lambda j: one_probe(j, a), jobs)), a, "probes")

    if a.corpus:
        a.cases = os.path.join(CENSUS, f"cases_{a.corpus}.json")
        if a.corpus == "a":
            a.a1 = os.path.join(CENSUS, "a1_a.jsonl")
    if not a.cases:
        ap.error("要 --corpus、--cases 或 --probes")
    with open(a.cases, encoding="utf-8") as f:
        cases = json.load(f)
    a1 = None
    if a.a1:
        with open(a.a1, encoding="utf-8") as f:
            a1 = {r["id"]: r for r in (json.loads(x) for x in f if x.strip())}
    omc_rc = {}
    sig = os.path.join(CENSUS, f"sig_{a.corpus}.jsonl") if a.corpus else ""
    if os.path.exists(sig):
        with open(sig, encoding="utf-8") as f:
            omc_rc = {r["id"]: r.get("rc") for r in (json.loads(x) for x in f if x.strip())}
    with ThreadPoolExecutor(a.jobs) as ex:
        return report(list(ex.map(lambda c: one_case(c, a, a1, omc_rc.get(c["id"])), cases)), a, a.corpus or "cases")


if __name__ == "__main__":
    sys.exit(main())
