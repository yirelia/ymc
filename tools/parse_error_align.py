#!/usr/bin/env python3
"""S1 验收 ③：语法错误用例的错误位置与 OMC 对齐（方案 §5 S1 验收 ③，"位置一致即可，措辞不要求"）。

两种模式。

一、默认模式（快，只看 `// Result:` 文本，不跑 omc）：
对语料里每个 .mo：从 `// Result:` 块提取 OMC 期望的 Error 位置（`[path:sl:sc-el:ec:writable] Error:`），
跑 `ymc --check-syntax` 取第一条错误的位置，比较 **起始 line:col**。

判定：
  MATCH      期望有语法错误且 ymc 第一条错误起点一致
  MISMATCH   期望有语法错误但 ymc 起点不同（打印两边的消息）
  MISSING    期望有语法错误但 ymc 没报错
  SPURIOUS   期望无语法错误（Result 里没有 PARSE_MSG 那些语法层消息）但 ymc 报错
  OK         期望无语法错误且 ymc 无错
  SKIP       无 Result 块 / .mos / MetaModelica（+g=MetaModelica）

二、`--testsuite-scan`（整个 testsuite，权威 = 实跑 omc 1.25.0）：
  1. 对 testsuite 下所有 .mo（MetaModelica / ParModelica / PDEModelica / Optimica 语法的跳过）跑 `ymc --check-syntax`；
  2. 候选 = ymc 拒绝的 ∪ ymc 接受、但某个 Result 块（.mo 自己的，或 .mos 里指向它的）记着语法层错误的；
     `--all` 时候选 = 全部（ymc 宽于 omc 的也找得出来）；
  3. 每个候选拷到临时目录，用 PATH 上的 omc 实跑（每例 120 s 超时，并发 ≤ 3）：
     `omc [--std=X] [--strict] probe.mos`，probe.mos = `parseFile("<copy>.mo"); getMessagesStringInternal();`
     （parseFile 走 `omc file.mo` 同一个 Parser.parse；不实例化、不装库、不查包名，所以快。只认 kind = syntax 的 Error，
     见 omc_parse）。`parseFile` 与 `omc file.mo` 的首错逐例对过（s1fix 探针 35 + 2 例，全同）；
  4. 比 omc 与 ymc 的**第一条 Error 起点** line:col。omc 结果按 (文件内容 sha1, 参数, omc 版本, 探法) 缓存在
     <repo>/.cache/parse_scan_omc.json，第二次起只跑 ymc（门里用）。
  判定：
    MATCH     两边都拒绝，首错起点一致
    MISMATCH  两边都拒绝，起点不同
    SPURIOUS  ymc 拒绝，omc 语法层通过
    MISSING   ymc 接受，omc 语法层拒绝（不带 --all 时只在"Result 记着语法错"的文件里找）
    STALE     ymc 接受，Result 记着语法错，但 omc 1.25.0 解析通过（Result 来自别的版本 / 别的参数，仅记账）
    KNOWN     已查明、修不了的分歧（known_reason：KNOWN_DIVERGENCES 表 + 规则，如 omc 1.25.0 没有 break 修饰），不计入失败
    TIMEOUT / CRASH  任一边超时 / ymc 非 0/1 退出
  末行一行汇总：`parse-scan: …`；MISMATCH / SPURIOUS / MISSING / TIMEOUT / CRASH 任一非 0 则退出码 1。

用法：
  python -I -X utf8 tools/parse_error_align.py                       # 默认语料
  python -I -X utf8 tools/parse_error_align.py --dir <dir> --show 40
  python -I -X utf8 tools/parse_error_align.py --testsuite-scan --ymc <ymc.exe> [--jobs 6] [--omc-jobs 3] [--json out.json]
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter, defaultdict

TS = "C:/dev/OpenModelica/testsuite"
DEFAULT_FILES = [
    f"{TS}/openmodelica/parser",
    f"{TS}/flattening/modelica/arrays/EmptyArray.mo",
    f"{TS}/flattening/modelica/arrays/ArrayModification1.mo",
    f"{TS}/flattening/modelica/arrays/ArrayModification2.mo",
    f"{TS}/flattening/modelica/modification/BreakComponentInvalid7.mo",
    f"{TS}/flattening/modelica/scodeinst/Ticket5821.mo",
]

# OMC 语法层错误的消息特征（其它 Error 是语义错误，不算）。来源（v1.25.0）：
#   parse.c handleParseError / handleLexerError 的固定前缀（Parse error / Parser error / Missing token /
#   No viable alternative / Expected token of type / Lexer failed / Lexer got）；
#   Modelica.g 里 ErrorType_syntax + ErrorLevel_error 的 c_add_source_message 与 modelicaParserAssert
#   （:340 首尾名、:602 connect、:915 下标修饰、:1081 算法注解、:1194/1199/1212/1232/1241 赋值/等式、
#   :1259 end if、:1533 偏应用、:1740/1780/1787 数太大、:1805 元组下标、:1824 空数组、:1857 点算符）；
#   BaseModelica_Lexer.g:201/239/248 的 &&、!、||。
PARSE_MSG = re.compile(
    r"Error: (Parse error|Parser error|Missing token|No viable alternative|Expected token of type|"
    r"Unexpected token|Lexer error|Lexer failed to recognize|Lexer got|mismatched input|extraneous input|"
    r"Subscripting modifiers is not allowed|The identifier at start and end are different|"
    r"Found the start of a connect equation|Annotations are not allowed in an algorithm list|"
    r"Algorithms can not contain equations|Modelica assignment statements are either|"
    r"Only function call expressions may stand alone|Equations can not contain assignments|"
    r"A singleton expression in an equation section|Expected 'end if'|"
    r"Function partial application expressions|Number is too large|Overflow: .* cannot be represented|"
    r".* cannot be represented by a double|Tuple expression can not be subscripted|"
    r"Empty array constructors are not valid|Dot operator is not allowed in function calls|"
    r"Please use '(and|not|or)' for logical)")
OMC_LOC = re.compile(r"\[[^\]\n]*?:(\d+):(\d+)-(\d+):(\d+):(?:writable|readonly)\]\s*(Error: .*)")
YMC_LOC = re.compile(r"\[[^\]\n]*?:(\d+):(\d+)-(\d+):(\d+):writable\]\s*(Error: .*)|\[(\d+):(\d+):(\d+)\]\s*(Error: .*)")


def read_text(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def result_block_lines(text):
    """`// Result:` … `// endResult` 之间去掉注释前缀的各行；无 Result 块返回 None。"""
    m = re.search(r"^// Result:\s*$(.*?)^// endResult", text, re.S | re.M)
    if not m:
        return None
    return [line[3:] if line.startswith("// ") else line[2:] if line.startswith("//") else line
            for line in m.group(1).splitlines()]


def expected_errors(text):
    """返回 Result 块里 OMC 的语法层错误 [(line, col, msg)]；None 表示无 Result 块。"""
    lines = result_block_lines(text)
    if lines is None:
        return None
    out = []
    for line in lines:
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


def omc_flags_from_cflags(text):
    """`// cflags:` 里影响 omc 语法层的参数：--std（Modelica_3_Lexer.g:59-62 的 stream / pure / impure 看 langStd）
    与 --strict（pure / impure 只在 langStd < 33 **且** strict 时降成 IDENT，Modelica_3_Lexer.g:61-62 v1.25.0）。"""
    m = CFLAGS_RE.search(text[:2000])
    if not m:
        return []
    flags = []
    for tok in m.group(1).split():
        if tok.startswith("--std=") or tok.startswith("+std="):
            flags.append("--std=" + tok.split("=", 1)[1])
        elif tok in ("--strict", "+strict", "--strict=true", "+strict=true"):
            flags.append("--strict")
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


def is_metamodelica(text):
    return "MetaModelica" in text[:400] or "+g=MetaModelica" in text or "-g=MetaModelica" in text


def run_default(args):
    files = collect(args.dir or DEFAULT_FILES)
    counts = Counter()
    rows = []
    for f in files:
        text = read_text(f)
        rel = os.path.relpath(f, TS).replace(os.sep, "/")
        if is_metamodelica(text):
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


# ---------------------------------------------------------------- --testsuite-scan

# 不是标准 Modelica 语法的用例：omc 要带 -g=MetaModelica / ParModelica / PDEModelica / Optimica 才解析得了，
# ymc 不支持这些方言。三条判据：整目录都是方言的（下面的目录表）；文件自己带标记（-g=…、uniontype、matchcontinue）；
# 被带 -g=… 的 .mos 装载（dialect_loaded_files）。
NON_MODELICA_DIRS = ("metamodelica/", "parmodelica/", "openmodelica/bootstrapping/", "openmodelica/rmltranslator/",
                     "julia/")
NON_MODELICA_MARK = re.compile(r"[-+]g=(MetaModelica|ParModelica|PDEModelica|Optimica)|PDEModelica|"
                               r"\bmatchcontinue\b|\buniontype\b")

# 已查明、照 omc 源码也修不了（或不该修）的分歧：rel → 原因。命中的判 KNOWN，不计入失败。
KNOWN_DIVERGENCES = {
}

BREAK_MOD_REASON = ("omc 1.25.0 的文法没有修饰里的 break（Modelica.g:893-899 argument 只有 element_modification_or_replaceable / "
                    "element_redeclaration；OMC e92e6c7c66 'support parsing of inheritance break' 2025-09-26 才加，首见 v1.26.0），"
                    "ymc 按 testsuite HEAD 的 Result 支持 break 修饰——去留待定，不在 S1 改")


def known_reason(rel, text, omc_first):
    """按规则认的已知分歧（比逐个列文件名稳：以后新加的同类用例自动归队）。"""
    if rel in KNOWN_DIVERGENCES:
        return KNOWN_DIVERGENCES[rel]
    if omc_first:
        lines = text.splitlines()
        line, col = omc_first[0], omc_first[1]
        # omc 首错正落在修饰实参里的 `break` 上（第 1 行的列 omc 少报 1，见 token.h omc_syntax_diag_loc）
        c = col + (1 if line == 1 else 0)
        if 1 <= line <= len(lines) and c >= 1 and lines[line - 1][c - 1:].startswith("break"):
            return BREAK_MOD_REASON
    return None


GRAMMAR_FLAG = re.compile(r"[-+]g=(MetaModelica|ParModelica|PDEModelica|Optimica)")
MOS_MO_REF = re.compile(r'"([^"\n]+?\.mo)"')


def dialect_loaded_files(ts):
    """被"切了语法"的 .mos 装载的 .mo：.mos 里出现 -g=MetaModelica / ParModelica / PDEModelica / Optimica，
    它引用的 "*.mo"（loadFile / loadFiles 的实参，相对 .mos 目录）就按那种方言解析，标准 Modelica 解析器不该拿来比。"""
    out = set()
    for root, _, names in os.walk(ts):
        for n in names:
            if not n.endswith(".mos"):
                continue
            text = read_text(os.path.join(root, n))
            if not GRAMMAR_FLAG.search(text):
                continue
            for m in MOS_MO_REF.finditer(text):
                p = os.path.join(root, m.group(1))
                if os.path.isfile(p):
                    out.add(os.path.normcase(os.path.abspath(p)))
    return out


def non_modelica(rel, text, path, dialect_files):
    if rel.startswith(NON_MODELICA_DIRS):
        return "dir"
    if is_metamodelica(text):
        return "MetaModelica"
    if os.path.normcase(os.path.abspath(path)) in dialect_files:
        return "mos -g"
    m = NON_MODELICA_MARK.search(text)
    return m.group(0) if m else None


MOS_LOC = re.compile(r"\[([^\]\n:]+?\.mo):(\d+):(\d+)-\d+:\d+:(?:writable|readonly)\]\s*(Error: .*)")


def result_parse_errors(ts):
    """整个 testsuite 的 Result 块里"位于某个 .mo 的语法层错误"：{abs .mo 路径: [(line, col, msg, 来源 rel)]}。
    .mo 看自己的 Result；.mos 看 Result 里带 .mo 路径的错误（testsuite 友好路径相对 testsuite 根，或相对 .mos 目录）。"""
    out = defaultdict(list)
    for root, _, names in os.walk(ts):
        for n in names:
            if not (n.endswith(".mo") or n.endswith(".mos")):
                continue
            p = os.path.join(root, n)
            text = read_text(p)
            if "// Result:" not in text:
                continue
            lines = result_block_lines(text) or []
            src_rel = os.path.relpath(p, ts).replace(os.sep, "/")
            for line in lines:
                if not PARSE_MSG.search(line):
                    continue
                for mm in MOS_LOC.finditer(line):
                    ref = mm.group(1)
                    if n.endswith(".mo"):  # .mo 自己的 Result：只认指向它自己的错误
                        if os.path.basename(ref) == n:
                            out[os.path.normcase(os.path.abspath(p))].append(
                                (int(mm.group(2)), int(mm.group(3)), mm.group(4).strip(), src_rel))
                        continue
                    for cand in (os.path.join(ts, ref), os.path.join(root, ref), os.path.join(root, os.path.basename(ref))):
                        if os.path.isfile(cand):
                            out[os.path.normcase(os.path.abspath(cand))].append(
                                (int(mm.group(2)), int(mm.group(3)), mm.group(4).strip(), src_rel))
                            break
    return out


def omc_version():
    try:
        p = subprocess.run(["omc", "--version"], capture_output=True, text=True, timeout=60)
        return p.stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


OMC_PROBE = "parseFile+syntax-kind"   # 进缓存键：探法变了旧缓存自动作废
OMC_MSG = re.compile(r'lineStart = (\d+),\s*columnStart = (\d+),.*?message = "((?:[^"\\]|\\.)*)",\s*'
                     r'kind = \.OpenModelica\.Scripting\.ErrorKind\.(\w+),\s*'
                     r'level = \.OpenModelica\.Scripting\.ErrorLevel\.(\w+)', re.S)


def omc_parse(path, flags, workdir, idx, timeout):
    """omc 实跑解析一份拷贝；返回 {"ok": bool, "first": [line, col, msg] | None, "timeout": bool}。
    `parseFile` 走 Parser.parse（FrontEnd/Parser.mo:59-72 v1.25.0）= 语法层 parsebuiltin + AbsynToSCode 检查，不装进程序、
    不查包名 / within（loadFile 会查，包里的文件拷出来就报 "Expected the package to have name …"）。
    只认 kind = syntax 的 Error（词法器 / Modelica.g / parse.c 报的，ErrorType_syntax）：AbsynToSCode 的
    （如 "Connect equations are not allowed in initial equation sections."，kind = translation）属于 S2，不算语法层。"""
    d = os.path.join(workdir, str(idx))
    os.makedirs(d, exist_ok=True)
    base = os.path.basename(path)
    shutil.copyfile(path, os.path.join(d, base))
    with open(os.path.join(d, "probe.mos"), "w", encoding="utf-8") as f:
        f.write(f'parseFile("{base}"); getMessagesStringInternal();\n')
    try:
        # 超时交给 subprocess（到点杀子进程）；Windows 上 CreateProcess 会先找到 System32\timeout.exe，不能拿它包。
        p = subprocess.run(["omc", *flags, "probe.mos"], cwd=d, capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
    except subprocess.TimeoutExpired:
        return {"ok": False, "first": None, "timeout": True}
    # getMessagesStringInternal 按"后报的在前"列（与 getErrorString 相反；s1fix 探针 p26 / p32 / q10 各两条错实证），
    # 所以时间上的第一条是列表里最后一条。
    first = None
    for m in OMC_MSG.finditer(p.stdout + p.stderr):
        if m.group(4) == "syntax" and m.group(5) == "error":
            first = [int(m.group(1)), int(m.group(2)), "Error: " + m.group(3).replace('\\"', '"')[:160]]
    return {"ok": first is None, "first": first, "timeout": False}


def run_scan(args):
    t0 = time.time()
    ts = os.path.abspath(TS)
    files = collect([ts])
    dialect_files = dialect_loaded_files(ts)
    todo, skipped = [], Counter()
    texts = {}
    for f in files:
        rel = os.path.relpath(f, ts).replace(os.sep, "/")
        text = read_text(f)
        why = non_modelica(rel, text, f, dialect_files)
        if why:
            skipped[why] += 1
            continue
        texts[f] = text
        todo.append(f)

    # 1. ymc 全量
    ymc_res = {}
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(ymc_errors, args.ymc, f, args.timeout, ymc_flags_from_cflags(texts[f])): f for f in todo}
        for fut in cf.as_completed(futs):
            ymc_res[futs[fut]] = fut.result()

    # 2. 候选（--all：全部；否则 ymc 拒绝的 ∪ Result 记着语法错的）
    expected = result_parse_errors(ts)
    cands = []
    for f in todo:
        rc, _ = ymc_res[f]
        key = os.path.normcase(os.path.abspath(f))
        if args.all or rc != 0 or key in expected:
            cands.append(f)

    # 3. omc 实跑（带缓存）
    cache_path = args.omc_cache
    cache = {}
    if cache_path and os.path.isfile(cache_path):
        try:
            with open(cache_path, encoding="utf-8") as fh:
                cache = json.load(fh)
        except (OSError, ValueError):
            cache = {}
    ver = omc_version()
    if "1.25.0" not in ver:
        print(f"警告：PATH 上的 omc 不是 1.25.0（{ver!r}）", file=sys.stderr)

    def ckey(f):
        with open(f, "rb") as fh:
            h = hashlib.sha1(fh.read()).hexdigest()
        return f"{h}|{' '.join(omc_flags_from_cflags(texts[f]))}|{ver}|{OMC_PROBE}"

    keys = {f: ckey(f) for f in cands}
    need = [f for f in cands if keys[f] not in cache]
    omc_runs = len(need)
    if need:
        work = tempfile.mkdtemp(prefix="ymc_parse_scan_")
        try:
            with cf.ThreadPoolExecutor(max_workers=max(1, min(3, args.omc_jobs))) as ex:
                futs = {ex.submit(omc_parse, f, omc_flags_from_cflags(texts[f]), work, i, args.omc_timeout): f
                        for i, f in enumerate(need)}
                for fut in cf.as_completed(futs):
                    cache[keys[futs[fut]]] = fut.result()
        finally:
            shutil.rmtree(work, ignore_errors=True)
        if cache_path:
            # 超时的不进缓存（机器忙时偶发），下次重跑；旧探法的条目顺手清掉
            keep = {k: v for k, v in cache.items() if not v.get("timeout") and k.endswith("|" + OMC_PROBE)}
            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
            with open(cache_path, "w", encoding="utf-8") as fh:
                json.dump(keep, fh, ensure_ascii=False, indent=0, sort_keys=True)

    # 4. 判定
    counts = Counter()
    rows = []
    for f in todo:
        rel = os.path.relpath(f, ts).replace(os.sep, "/")
        rc, got = ymc_res[f]
        if f not in keys:
            counts["OK"] += 1
            continue
        o = cache[keys[f]]
        yfirst = got[0] if got else None
        ofirst = o["first"]
        if rc == "timeout" or o["timeout"]:
            v = "TIMEOUT"
        elif rc not in (0, 1):
            v = "CRASH"
        elif rc == 1 and not o["ok"]:
            v = "MATCH" if ofirst and yfirst and (ofirst[0], ofirst[1]) == (yfirst[0], yfirst[1]) else "MISMATCH"
        elif rc == 1:
            v = "SPURIOUS"
        elif not o["ok"]:
            v = "MISSING"
        elif os.path.normcase(os.path.abspath(f)) in expected:
            v = "STALE"
        else:
            counts["OK"] += 1   # --all：两边都解析通过
            continue
        why = known_reason(rel, texts[f], ofirst) if v in ("MISMATCH", "SPURIOUS", "MISSING") else None
        if why:
            v = "KNOWN"
        counts[v] += 1
        rows.append(dict(rel=rel, verdict=v, ymc=list(yfirst) if yfirst else None, omc=ofirst, known=why,
                         result=[list(e[:3]) + [e[3]] for e in expected.get(os.path.normcase(os.path.abspath(f)), [])][:2]))

    bad_kinds = ("MISMATCH", "SPURIOUS", "MISSING", "TIMEOUT", "CRASH")
    bad = [r for r in rows if r["verdict"] in bad_kinds]
    # KNOWN 不逐例列（按原因汇总一行），要看明细用 --json
    show = [r for r in rows if r["verdict"] in bad_kinds + (("STALE",) if args.show_stale else ())]
    for r in show[: args.show]:
        print(f"  {r['verdict']:<9} {r['rel']}")
        if r["omc"]: print(f"            omc: {r['omc'][0]}:{r['omc'][1]} {r['omc'][2][:110]}")
        elif r["verdict"] in ("SPURIOUS", "STALE"): print("            omc: (解析通过)")
        if r["ymc"]: print(f"            ymc: {r['ymc'][0]}:{r['ymc'][1]} {r['ymc'][2][:110]}")
    known_reasons = Counter(r["known"] for r in rows if r["verdict"] == "KNOWN")
    for why, n in known_reasons.most_common():
        print(f"  KNOWN ×{n}: {why}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(rows, fh, ensure_ascii=False, indent=1)
    ymc_rej = sum(1 for f in todo if ymc_res[f][0] != 0)
    print("  跳过（非标准 Modelica 语法）: " + ", ".join(f"{k} {v}" for k, v in skipped.most_common()))
    print(f"parse-scan: {len(files)} files (skip {sum(skipped.values())}) ymc-reject {ymc_rej} | "
          f"MATCH {counts['MATCH']} MISMATCH {counts['MISMATCH']} SPURIOUS {counts['SPURIOUS']} "
          f"MISSING {counts['MISSING']} KNOWN {counts['KNOWN']} STALE {counts['STALE']} "
          f"TIMEOUT {counts['TIMEOUT']} CRASH {counts['CRASH']} OK {counts['OK']} | "
          f"omc runs {omc_runs}/{len(cands)}{' (all)' if args.all else ''} | {time.time() - t0:.0f}s")
    return 0 if not bad else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--dir", action="append", default=None, help="可多次给；默认 openmodelica/parser + 5 个 flattening 用例")
    ap.add_argument("--timeout", type=float, default=20.0, help="ymc 每例超时（秒）")
    ap.add_argument("--show", type=int, default=60)
    ap.add_argument("--testsuite-scan", action="store_true", help="整个 testsuite、实跑 omc 对拍（见文件头）")
    ap.add_argument("--jobs", type=int, default=6, help="--testsuite-scan：ymc 并发")
    ap.add_argument("--omc-jobs", type=int, default=3, help="--testsuite-scan：omc 并发（上限 3）")
    ap.add_argument("--omc-timeout", type=float, default=120.0)
    ap.add_argument("--omc-cache", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                        ".cache", "parse_scan_omc.json"),
                    help="omc 结果缓存（空串 = 不用缓存）")
    ap.add_argument("--show-stale", action="store_true", help="也列出 STALE")
    ap.add_argument("--all", action="store_true",
                    help="--testsuite-scan：ymc 接受的也全部交给 omc（冷缓存约十几分钟，之后走缓存），找出 ymc 宽于 omc 的")
    ap.add_argument("--json", default=None, help="--testsuite-scan：逐例结果写成 JSON")
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)  # Windows CreateProcess 不接受带正斜杠的相对路径
    return run_scan(args) if args.testsuite_scan else run_default(args)


if __name__ == "__main__":
    sys.exit(main())
