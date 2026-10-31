#!/usr/bin/env python3
"""S5 结构参考生成（ADR-008 D4）：跑 omc 1.25.0 新后端，存原文 / 签名，给准入普查（bdae_census.py）用。

命令固定为 D4 的：
  omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo M.mo
每个模型一个临时 cwd，超时 120 s，stdout / stderr 直接落盘（防 GB 级输出撑爆内存，超过 --max-out 即杀）。

两种语料：
  extract   从 NBackend/{basics,alias,event_handling} 的 .mos 和 equations/parameters 取模型（内联 loadString 的也取），
            写 cases.json。脚本里的 setCommandLineOptions / cflags 只记录（script_flags），不进命令——参考口径固定。
  sweep     由 nf_live_sweep 的 JSON 取 verdict=PASS 的记录（A1 证据），写 cases.json；文件处理同 nf_live_sweep
            （去 `-d=-newInst` 注解，目标是文件里最后一个类）。

  run       按 cases.json 跑。--raw 时把 model.mo / out.txt / err.txt 存到 <refs>/<id>/（语料 a）；不带 --raw 只留签名
            （语料 b，防磁盘膨胀；每条记录有 out_bytes，可算原文总量）。结果逐条追加到 --out（JSONL），重跑自动跳过已完成的。

成败判据（`.mo` + `-s` 路线，Main.mo translateFile）：这条路线丢弃 translateModel 的 success 返回值，失败走 fail()，
落到 matchcontinue 的 "Error processing file: " 分支后 exit 1；所以 a3 = exit 0 且 stderr 无 `Error:` / `Internal error`。

用法：
  python -I tools/bdae_refgen.py extract --cases <cases.json>
  python -I tools/bdae_refgen.py sweep --from <ts_cur.json> --cases <cases.json>
  python -I tools/bdae_refgen.py run --cases <cases.json> --out <sig.jsonl> [--raw] [--jobs 8] [--limit N] [--ids a,b]
  python -I tools/bdae_refgen.py a1 --cases <cases.json> --out <a1.jsonl> --ymc <ymc.exe>     # 语料 a 的 A1

基建工具，Claude 提供。
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import structcompare as sc  # noqa: E402

OMC = r"C:\Program Files\OpenModelica1.25.0-64bit\bin\omc.exe"
TESTSUITE = r"C:\dev\OpenModelica\testsuite"
REFS = r"C:\dev\ymc-refs\omc-installed\1.25.0\bdae"
FLAGS = ["--newBackend", "-s", "--simCodeTarget=None", "-d=optdaedump,bltdump,backenddaeinfo"]
NB_DIRS = ["simulation/modelica/NBackend/basics", "simulation/modelica/NBackend/alias",
           "simulation/modelica/NBackend/event_handling"]
OTHER_DIRS = ["simulation/modelica/equations", "simulation/modelica/parameters"]
_ANN = r'annotation\s*\(\s*__OpenModelica_commandLineOptions\s*=\s*"-d=-newInst"\s*\)'
_ONLY_LINE = re.compile(r"^\s*" + _ANN + r"\s*;\s*$")
_INLINE = re.compile(r"\s*" + _ANN)
_ANY_OPTS = re.compile(r'__OpenModelica_commandLineOptions\s*=\s*"([^"]*)"')
_ERR = re.compile(r"^(?:\[[^\]]*\] )?(Error|Internal error)\b.*$", re.M)
_WARN = re.compile(r"^(?:\[[^\]]*\] )?Warning:.*$", re.M)
_CALL = re.compile(r"(?<![\w.$'])([A-Za-z_][\w.]*)\(")
_NOT_CALLS = {"if", "then", "else", "elseif", "and", "or", "not", "when", "for", "while", "in", "loop"}


def strip_annotation(text):
    """同 nf_live_sweep.strip_annotation(take_plain=True)；这里不 import 它，免得把 tests/omc/run.py 的环境带进来。"""
    opts = _ANY_OPTS.findall(text)
    if not opts:
        return text if "__OpenModelica_commandLineOptions" not in text else None
    if any(o != "-d=-newInst" for o in opts):
        return None
    lines = ["" if _ONLY_LINE.match(ln) else _INLINE.sub("", ln) for ln in text.split("\n")]
    stripped = "\n".join(lines)
    return None if "__OpenModelica_commandLineOptions" in stripped else stripped


# ---------------------------------------------------------------- .mos 抽取
def _read_string(s, i):
    """s[i] 是开引号。返回 (解转义后的串, 闭引号之后的下标)。"""
    out, i = [], i + 1
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"n": "\n", "t": "\t"}.get(n, n))
            i += 2
        elif c == '"':
            return "".join(out), i + 1
        else:
            out.append(c)
            i += 1
    raise ValueError("unterminated string")


def scan_mos(text):
    """顺序扫 .mos 顶层语句，返回 (cflags, [(函数名, 第一实参)])。`// Result:` 之后是期望输出，不扫。"""
    m = re.search(r"^//\s*cflags:(.*)$", text, re.M)
    cflags = m.group(1).strip() if m else ""
    cut = re.search(r"^//\s*Result:", text, re.M)
    s = text[:cut.start()] if cut else text
    calls, i = [], 0
    wanted = {"loadString", "loadFile", "loadModel", "setCommandLineOptions", "setDebugFlags",
              "simulate", "buildModel", "translateModel"}
    while i < len(s):
        c = s[i]
        if s.startswith("//", i):
            j = s.find("\n", i)
            i = len(s) if j < 0 else j + 1
        elif s.startswith("/*", i):
            j = s.find("*/", i)
            i = len(s) if j < 0 else j + 2
        elif c == '"':
            _, i = _read_string(s, i)
        elif c.isalpha() or c == "_":
            j = i
            while j < len(s) and (s[j].isalnum() or s[j] in "_."):
                j += 1
            word, i = s[i:j], j
            k = i
            while k < len(s) and s[k] in " \t":
                k += 1
            if word in wanted and k < len(s) and s[k] == "(":
                k += 1
                while k < len(s) and s[k] in " \t\r\n":
                    k += 1
                if s[k] == '"':
                    arg, i = _read_string(s, k)
                else:
                    e = k
                    while e < len(s) and s[e] not in ",)":
                        e += 1
                    arg, i = s[k:e].strip(), e
                calls.append((word, arg))
        else:
            i += 1
    return cflags, calls


def extract_cases():
    """语料 (a)。每个 (脚本, 目标类) 一条；id = <目录>/<脚本名>#<类>。"""
    cases, covered = [], set()
    for d in NB_DIRS + OTHER_DIRS:
        ad = os.path.join(TESTSUITE, d)
        for f in sorted(os.listdir(ad)):
            if not f.endswith(".mos"):
                continue
            with open(os.path.join(ad, f), encoding="utf-8", errors="replace") as h:
                cflags, calls = scan_mos(h.read())
            srcs, libs, flags, seen = [], [], [], set()
            for fn, arg in calls:
                if fn == "loadString":
                    srcs.append({"inline": arg})
                elif fn == "loadFile":
                    srcs.append({"file": arg})
                    covered.add(os.path.normpath(os.path.join(ad, arg)))
                elif fn == "loadModel":
                    libs.append(arg)
                elif fn in ("setCommandLineOptions", "setDebugFlags"):
                    flags.append(arg)
                elif arg and arg not in seen:
                    seen.add(arg)
                    cases.append({"id": f"{d.split('/')[-1]}/{f[:-4]}#{arg}", "dir": d, "script": f, "cls": arg,
                                  "srcs": list(srcs), "libs": list(libs),
                                  "script_flags": " ".join([cflags] + flags).strip(),
                                  "origin": "nb" if d in NB_DIRS else
                                  ("mos-inline" if any("inline" in x for x in srcs) else "mo-file")})
    for d in OTHER_DIRS:          # 没有脚本驱动的 .mo：目标是最后一个类（同 nf_live_sweep）
        ad = os.path.join(TESTSUITE, d)
        for f in sorted(os.listdir(ad)):
            p = os.path.normpath(os.path.join(ad, f))
            if f.endswith(".mo") and p not in covered:
                cases.append({"id": f"{d.split('/')[-1]}/{f[:-3]}", "dir": d, "script": "", "cls": "",
                              "srcs": [{"file": f}], "libs": [], "script_flags": "", "origin": "mo-file"})
    return cases


def sweep_cases(path):
    with open(path, encoding="utf-8") as f:
        recs = json.load(f)["records"]
    return [{"id": r["path"], "path": r["path"], "cls": "", "libs": [], "script_flags": "", "origin": "ts-pass"}
            for r in recs if r["verdict"] == "PASS"]


def case_text(c):
    """取模型文本。语料 (a) 拼 srcs；语料 (b) 同 nf_live_sweep 去注解。"""
    if "path" in c:
        with open(os.path.join(TESTSUITE, c["path"]), encoding="utf-8", errors="replace") as f:
            return strip_annotation(f.read())
    parts = []
    for s in c["srcs"]:
        if "inline" in s:
            parts.append(strip_annotation(s["inline"]) or s["inline"])
        else:
            with open(os.path.join(TESTSUITE, c["dir"], s["file"]), encoding="utf-8", errors="replace") as f:
                parts.append(strip_annotation(f.read()) or "")
    return "\n".join(parts)


# ---------------------------------------------------------------- 跑 omc
def run_omc(args, cwd, timeout, max_out):
    """stdout / stderr 落盘。返回 (rc 或 None, 状态, 秒)。"""
    out, err = os.path.join(cwd, "out.txt"), os.path.join(cwd, "err.txt")
    t0 = time.time()
    status = "done"
    with open(out, "wb") as fo, open(err, "wb") as fe:
        p = subprocess.Popen(args, cwd=cwd, stdout=fo, stderr=fe, stdin=subprocess.DEVNULL)
        while True:
            try:
                p.wait(0.2)
                break
            except subprocess.TimeoutExpired:
                if time.time() - t0 > timeout:
                    status = "timeout"
                elif os.path.getsize(out) > max_out:
                    status = "oversize"
                if status != "done":
                    p.kill()
                    p.wait()
                    break
    return (p.returncode if status == "done" else None), status, round(time.time() - t0, 2)


def _read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def _hist(items, key):
    c = collections.Counter()
    for it in items:
        c[it[key]] += 1
    return dict(c)


def lowering_shape(body):
    """(After Lowering) 的形状：桶、标签、调用名，以及 A2 判不出的"非标量"痕迹。"""
    v, e = sc.parse_var_data(body), sc.parse_eq_data(body)
    vb = v["buckets"]
    ev = [i for b in e["buckets"].values() for i in b["items"]]
    vi = [i for k, b in vb.items() if k != "Artificial" for i in b["items"]]
    calls = collections.Counter()
    for i in ev:
        for name in _CALL.findall(i["text"]):
            if name not in _NOT_CALLS:
                calls[name] += 1
    nested = collections.Counter()
    for i in ev:
        for line in i["text"].split("\n")[1:]:
            ls = line.strip()
            w = ls.split(" ")[0] if ls else ""
            if w in ("when", "if", "for", "reinit", "assert", "terminate", "elsewhen", "elseif", "else"):
                nested[w] += 1
            elif ":=" in ls:
                nested[":="] += 1
    return {"var_tags": _hist(vi, "tag"), "var_buckets": {k: b["count"] for k, b in vb.items()},
            "eq_tags": _hist(ev, "tag"), "eq_buckets": {k: b["count"] for k, b in e["buckets"].items()},
            "calls": dict(calls), "nested": dict(nested),
            "array_vars": sum(1 for i in vi if i["size"] != 1 or re.match(r"^(?:\w+ )*[^ ]*\[", i["decl"])),
            "record_vars": vb.get("Record", {}).get("count", 0),
            "array_eqs": sum(1 for i in ev if i["tag"] in ("[ARRY]", "[FOR-]")),
            "record_eqs": sum(1 for i in ev if i["tag"] in ("[RECD]", "[TUPL]")),
            "multi_size_eqs": sum(1 for i in ev if i["tag"] in ("[WHEN]", "[ALGO]", "[-IF-]") and i["size"] > 1)}


def summarize(c, rc, status, secs, out, err):
    rec = {"id": c["id"], "cls": c.get("cls", ""), "origin": c.get("origin", ""), "script_flags": c.get("script_flags", ""),
           "status": status, "rc": rc, "seconds": secs, "out_bytes": len(out), "err_bytes": len(err)}
    sections, tail = sc.split_bdae_sections(out)
    bodies = sc.pre_stage_bodies(sections)
    info = sc.parse_backenddaeinfo(err)
    counts = {k: info[k] for k in ("equations", "variables") if k in info}
    rec["counts"] = {k: list(v) for k, v in counts.items()}
    rec["errors"] = [m.group(0)[:300] for m in _ERR.finditer(err)][:5]
    rec["warnings"] = [m.group(0)[:200] for m in _WARN.finditer(err)][:3]
    rec["a2_eq"] = "equations" in counts and counts["equations"][0] == counts["equations"][1]
    rec["a2_var"] = "variables" in counts and counts["variables"][0] == counts["variables"][1]
    rec["a2"] = rec["a2_eq"] and rec["a2_var"]
    rec["a3"] = status == "done" and rc == 0 and not rec["errors"]
    rec["stages"] = [s for s in sc.PRE_STAGES if s in bodies]
    rec["last_section"] = sections[-1]["title"] if sections else ""
    rec["n_sections"] = len(sections)
    rec["stats_printed"] = "Partition statistics" in err
    rec["flat_printed"] = bool(tail) and not tail.startswith(("Error processing", "File does not"))
    rec["stats"] = {k: list(v) for k, v in info.items() if k not in ("equations", "variables")} \
        if rec["stats_printed"] else {}
    rec["identity"] = sc.stage_identity(bodies)
    rec["stage_sha"] = {k: hashlib.sha1(v.encode("utf-8")).hexdigest()[:16] for k, v in bodies.items()}
    if "After Lowering" in bodies:
        rec["shape"] = lowering_shape(bodies["After Lowering"])
        rec["sig_lower"] = sc.project_5a(bodies["After Lowering"])
    if "Events" in bodies:
        rec["sig_5a"] = sc.project_5a(bodies["Events"], counts)
        rec["events"] = sc.parse_event_counts(bodies["Events"])
    b5 = sc.project_5b(sections)
    if b5:
        rec["sig_5b"] = b5
    return rec


_lock = threading.Lock()


def one(c, a):
    text = case_text(c)
    if text is None:
        return {"id": c["id"], "status": "skipped-annotation"}
    tmp = tempfile.mkdtemp(prefix="bdae_", dir=a.tmp)
    try:
        name = "model.mo"
        with open(os.path.join(tmp, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        args = [OMC] + FLAGS + ([f"-i={c['cls']}"] if c.get("cls") else []) + [name] + list(c.get("libs", []))
        rc, status, secs = run_omc(args, tmp, a.timeout, a.max_out)
        out, err = _read(os.path.join(tmp, "out.txt")), _read(os.path.join(tmp, "err.txt"))
        rec = summarize(c, rc, status, secs, out, err)
        if a.raw:
            d = os.path.join(a.raw_root, re.sub(r"[^\w.#-]", "_", c["id"].replace("/", "__").replace("#", "__")))
            os.makedirs(d, exist_ok=True)
            for n in (name, "out.txt", "err.txt"):
                shutil.copyfile(os.path.join(tmp, n), os.path.join(d, n))
            with open(os.path.join(d, "sig.json"), "w", encoding="utf-8") as f:
                json.dump(rec, f, ensure_ascii=False, indent=1)
            rec["raw_dir"] = os.path.basename(d)
            rec["model_sha256"] = hashlib.sha256(text.encode("utf-8")).hexdigest()
        return rec
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def move_class_last(text, cls):
    """把顶层类 cls 挪到文本末尾：`ymc X.mo` / `omc X.mo` 都压平最后一个类，同一文件多个目标时靠它选类。"""
    head = re.compile(r"^[ \t]*(?:(?:encapsulated|partial|final|expandable|impure|pure|operator)\s+)*"
                      r"(?:model|class|block|connector|record|package|function|type)\s+" + re.escape(cls) + r"\b", re.M)
    m = head.search(text)
    if not m:
        return None
    e = re.compile(r"^[ \t]*end\s+" + re.escape(cls) + r"\s*;", re.M).search(text, m.end())
    short = re.compile(r"\s*(?:\([^;]*\))?\s*=").match(text, m.end())
    if e and not short:
        end = e.end()
    else:
        end = text.index(";", m.end()) + 1
    return text[:m.start()] + text[end:] + "\n" + text[m.start():end] + "\n"


def a1_one(c, a):
    """A1：ymc 压平与默认路径的 omc 压平逐字节一致（同 nf_live_sweep 的判据，只比 stdout）。语料 (b) 不用它，ts_cur 就是证据。"""
    text, cls = case_text(c), c.get("cls", "")
    tmp = tempfile.mkdtemp(prefix="bdae_a1_", dir=a.tmp)
    try:
        ycmd, ocmd = [a.ymc, "model.mo"], [OMC, "model.mo"]
        if "." in cls:           # 限定名目标：ymc 只有 --load-model 路线能选类
            top = cls.split(".")[0]
            with open(os.path.join(tmp, top + ".mo"), "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            ycmd = [a.ymc, f"--modelica-path={tmp}", f"--load-model={top}", f"--class={cls}", "--dump-after=flat"]
            ocmd = [OMC, f"-i={cls}", "model.mo"]
        elif cls:
            text = move_class_last(text, cls) or text
        with open(os.path.join(tmp, "model.mo"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)

        def run(cmd):
            try:
                r = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True, timeout=a.timeout,
                                   encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL)
                return r.returncode, r.stdout.replace("\r\n", "\n").strip()
            except subprocess.TimeoutExpired:
                return None, ""
        oc, oo = run(ocmd)
        yc, yo = run(ycmd)
        ok_o = oc is not None and oo.startswith(("class", "function", "impure function"))
        why = ("omc-reject" if not ok_o else "ymc-skip" if yc == 66 else "ymc-error" if yc != 0
               else "ok" if yo == oo else "diff")
        return {"id": c["id"], "a1": why == "ok", "why": why}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def cmd_a1(a):
    with open(a.cases, encoding="utf-8") as f:
        cases = json.load(f)
    os.makedirs(a.tmp, exist_ok=True)
    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        recs = list(ex.map(lambda c: a1_one(c, a), cases))
    with open(a.out, "w", encoding="utf-8") as f:
        for r in recs:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(collections.Counter(r["why"] for r in recs))
    return 0


def write_manifest(a, recs, ver):
    path = os.path.join(a.raw_root, "manifest.json")
    os.makedirs(a.raw_root, exist_ok=True)
    man = {"schema_version": 1, "oracle": "omc-installed", "oracle_version": ver,
           "command": ["omc"] + FLAGS + ["[-i=<cls>]", "model.mo", "[libs]"],
           "gen_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
           "entries": sorted(({"id": r["id"], "raw_dir": r.get("raw_dir", ""), "model_hash": r.get("model_sha256", ""),
                               "script_flags": r.get("script_flags", ""), "rc": r.get("rc"), "status": r.get("status")}
                              for r in recs), key=lambda e: e["id"])}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1, sort_keys=True)


def cmd_run(a):
    with open(a.cases, encoding="utf-8") as f:
        cases = json.load(f)
    if a.ids:
        want = set(a.ids.split(","))
        cases = [c for c in cases if c["id"] in want]
    done = {}
    if os.path.exists(a.out):
        with open(a.out, encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    r = json.loads(line)
                    done[r["id"]] = r
    todo = [c for c in cases if c["id"] not in done]
    if a.limit:
        todo = todo[:a.limit]
    ver = subprocess.run([OMC, "--version"], capture_output=True, text=True, timeout=30).stdout.strip()
    print(f"{ver}: {len(todo)} to run, {len(done)} done, jobs={a.jobs}", flush=True)
    os.makedirs(a.tmp, exist_ok=True)

    def job(c):
        try:
            r = one(c, a)
        except Exception as e:     # 解析器的毛病不能吃掉一次长跑；单独记，事后修
            r = {"id": c["id"], "status": "tool-error", "error": repr(e)[:300]}
        with _lock:
            with open(a.out, "a", encoding="utf-8") as f:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
            done[r["id"]] = r
            if len(done) % 100 == 0:
                print(f"  {len(done)} done", flush=True)
        return r

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        list(ex.map(job, todo))
    if a.raw:
        write_manifest(a, list(done.values()), ver)
    print("finished:", collections.Counter(r.get("status") for r in done.values()), flush=True)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract")
    p.add_argument("--cases", required=True)
    p = sub.add_parser("sweep")
    p.add_argument("--from", dest="src", required=True)
    p.add_argument("--cases", required=True)
    p = sub.add_parser("run")
    p.add_argument("--cases", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--raw", action="store_true", help="存原文（语料 a）")
    p.add_argument("--raw-root", default=REFS)
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--max-out", type=int, default=512 * 1024 * 1024)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--ids", default="")
    p.add_argument("--tmp", default=os.path.join(tempfile.gettempdir(), "ymc_bdae"))
    p = sub.add_parser("a1")
    p.add_argument("--cases", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--ymc", required=True)
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--tmp", default=os.path.join(tempfile.gettempdir(), "ymc_bdae"))
    a = ap.parse_args()
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "a1":
        return cmd_a1(a)
    cases = extract_cases() if a.cmd == "extract" else sweep_cases(a.src)
    with open(a.cases, "w", encoding="utf-8") as f:
        json.dump(cases, f, ensure_ascii=False, indent=1)
    print(f"{len(cases)} cases -> {a.cases}")
    print(collections.Counter(c.get("origin") for c in cases))
    return 0


if __name__ == "__main__":
    sys.exit(main())
