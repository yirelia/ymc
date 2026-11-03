"""结构级比对（FR-015）：解析 OMC 的压平/因果化结构输出，供 S3/S5 对比。

解析对象（研究底稿 R6）：
- checkModel：`Class X has N equation(s) and M variable(s).`（CevalScriptBackend.mo:6184）。
- 新后端 -d=backenddaeinfo 的 4 段 Notification（NBackendDAE.mo:1584-1694），`标量数 (元素数)`。
- 新后端 -d=bltdump 的块头 `BLOCK i: <kind> (...)`（NBStrongComponent.mo:186-223）。
- Base Modelica（omc -f）：`model`/`equation` 段的变量与方程粗计数。

注意：新旧后端结构统计不可直接互比（R6§8）；块头字段随 omc 版本变化，对比须记录版本。

基建工具，Claude 提供。Dymola 侧统计解析见 dymola_driver.py。
"""
from __future__ import annotations

import re

_CHECKMODEL = re.compile(
    r"has\s+(\d+)\s+equation\(s\)\s+and\s+(\d+)\s+variable\(s\)")
_TRIVIAL = re.compile(r"(\d+)\s+of these are trivial")
_INFO_LINE = re.compile(r"Number of ([^:]+):\.*\s*(\d+)(?:\s*\((\d+)\))?")
_EQVAR = re.compile(r"Number of (equations|variables):\s*(\d+)\s*\((\d+)\)")
_BLOCK = re.compile(r"BLOCK\s+(-?\d+):\s+([A-Za-z ]+?)(?:\s*\((.*)\))?\s*$")


def parse_checkmodel(text):
    m = _CHECKMODEL.search(text)
    if not m:
        return None
    out = {"equations": int(m.group(1)), "variables": int(m.group(2))}
    t = _TRIVIAL.search(text)
    if t:
        out["trivial"] = int(t.group(1))
    return out


def parse_backenddaeinfo(text):
    """解析 backenddaeinfo 的计数行。返回 {名字: (标量数, 元素数或 None)}。"""
    out = {}
    # 前端后计数 "Number of equations: 3 (3)"
    for m in _EQVAR.finditer(text):
        out[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    # 分区/变量/分量的 " * Number of ...: ..... N" 或 "N (n) ..."（冒号后可有空格+点导引）
    for line in text.splitlines():
        m = re.search(r"Number of ([^:]+?):\s*\.*\s*(\d+)(?:\s*\((\d+)\))?", line)
        if m:
            key = m.group(1).strip()
            scal = int(m.group(2))
            elem = int(m.group(3)) if m.group(3) else None
            out.setdefault(key, (scal, elem))
    return out


def parse_blt_headers(text):
    """返回块头列表 [(index, kind, fields_str)]（供结构级 diff：块头种类序列）。"""
    heads = []
    for line in text.splitlines():
        m = _BLOCK.match(line.strip())
        if m:
            idx = int(m.group(1))
            kind = m.group(2).strip()
            fields = (m.group(3) or "").strip()
            heads.append((idx, kind, fields))
    return heads


def parse_base_modelica(text):
    """粗解析 omc -f 的 Base Modelica：统计 model 段内变量声明数与 equation 段方程数。"""
    lines = text.splitlines()
    in_model = False
    section = None  # None|equation|algorithm|initial
    n_vars = 0
    n_eqs = 0
    for raw in lines:
        s = raw.strip()
        if re.match(r"^model\s", s):
            in_model = True
            section = None
            continue
        if s.startswith("end ") and in_model:
            in_model = False
            continue
        if not in_model:
            continue
        if s in ("equation", "initial equation"):
            section = "equation"
            continue
        if s in ("algorithm", "initial algorithm"):
            section = "algorithm"
            continue
        if not s:
            continue
        if section is None:
            # 变量声明行（以 ; 结尾、含类型或标识符）
            if s.endswith(";"):
                n_vars += 1
        elif section == "equation":
            if s.endswith(";"):
                n_eqs += 1
    return {"variables": n_vars, "equations": n_eqs}


def compare_counts(a, b, keys=("equations", "variables", "states")):
    """比较两个计数 dict，返回 {key: (a, b, equal)}。用于三方结构级对比表。"""
    out = {}
    for k in keys:
        va, vb = a.get(k), b.get(k)
        out[k] = (va, vb, va == vb)
    return out


# ---------------------------------------------------------------- NB 逐模块 dump（ADR-008 D3 / D4）
# 版式来源（v1.25.0）：段头 Util/StringUtil.mo:52-57 headline_1；段体 NBackendDAE.mo:150-169；
# VarData NBVariable.mo:2187 / :1575；EqData NBEquation.mo:4416 / :3847；行 NBEquation.mo:968；
# 块 NBStrongComponent.mo:166；撕裂集 NBTearing.mo:97。
_HEADLINE1 = re.compile(r"^(#{6,})\n\n    (.+)\n\n(#{6,})\n", re.M)
_TITLE = re.compile(r"^(?:\[(\w+)\] )?(.+?): \(?(.*?)\)?$")
_FLAT_START = re.compile(
    r"^(?:class |model |function |impure function |package |Error processing file:|File does not exist:)", re.M)
_VAR_BUCKET = re.compile(r"^(.+) Variables \((\d+)/(\d+)\)$")
_EQ_BUCKET = re.compile(r"^(.+) Equations \((\d+)/(\d+)\)$")
_ITEM = re.compile(r"^\((\d+)\)\s+(\[[^\]]+\](?:\[[^\]]+\])?) \((-?\d+)\)\s?(.*)$")
_CONT = re.compile(r"^\((\d+)\)\s+\[----\] ?(.*)$")
_RES_NAME = re.compile(r"\((\$[A-Z_]*RES_[A-Z]+_\d+|\$[A-Z]+_\d+)\)\s*;?\s*$")
_PARTITION = re.compile(r"^  \((\d+)\) (.+) Partition$")
_BLOCK_HEAD = re.compile(r"^BLOCK (\d+): (.+?)(?: \((.*)\))?$")
_DECL_WORDS = {"protected", "public", "parameter", "constant", "discrete", "input", "output",
               "flow", "stream", "final", "inner", "outer", "replaceable", "redeclare", "structural",
               "evaluated", "enumeration"}      # Type.toString 打 `enumeration E(a, b) x`

# NB 的预处理段序（NBackendDAE.mo:268-278 + SimCodeMain.mo:1464-1466 的 "(After Lowering)"）
PRE_STAGES = ("After Lowering", "Bindings", "FunctionAlias", "Early Inline", "Simplify 1", "Alias",
              "Simplify 2", "Remove Stream", "Detect States", "Events")


def stage_dir(module):
    """模块名 → 目录名：`Simplify 1` → `simplify_1`。"""
    return module.lower().replace(" ", "_")


def split_bdae_sections(text):
    """把 `-d=optdaedump,bltdump` 的 stdout 切成段。返回 (sections, tail)。

    section = {"title", "tag", "name", "module", "body"}；tag 是 ODE/INI/... 或 None（BackendDAE 段）。
    tail 是最后一段之后的压平文本（或 "Error processing file:" 之后的失败尾巴）。"""
    text = text.replace("\r\n", "\n")
    marks = [m for m in _HEADLINE1.finditer(text)
             if len(m.group(1)) == len(m.group(3)) == len(m.group(2)) + 8]
    secs = []
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        title = m.group(2)
        t = _TITLE.match(title)
        secs.append({"title": title, "tag": t.group(1) if t else None, "name": t.group(2) if t else title,
                     "module": t.group(3) if t else "", "body": text[m.end():end]})
    tail = ""
    if secs:
        m = _FLAT_START.search(secs[-1]["body"])
        if m:
            tail = secs[-1]["body"][m.start():]
            secs[-1]["body"] = secs[-1]["body"][:m.start()]
    return secs, tail


def pre_stage_bodies(sections):
    """{模块名: 段体}，只取 `BackendDAE: (...)` 段里 PRE_STAGES 的第一次出现。"""
    out = {}
    for s in sections:
        if s["tag"] is None and s["name"] == "BackendDAE" and s["module"] in PRE_STAGES:
            out.setdefault(s["module"], s["body"])
    return out


def stage_section_texts(text):
    """{模块名: 段原文}，模块取自 PRE_STAGES，同名段只留第一次。段原文 = 头线（headline_1，StringUtil.mo:52）+ split_bdae_sections 给的段体。

    这就是 `ymc --dump-after=<模块>` 要打的字节范围：从头线第一个 `#` 到下一段头线之前（段体末尾带 EqData 的空行）。"""
    out = {}
    for s in split_bdae_sections(text)[0]:
        if s["tag"] is None and s["name"] == "BackendDAE" and s["module"] in PRE_STAGES and s["module"] not in out:
            bar = "#" * (len(s["title"]) + 8)
            out[s["module"]] = bar + "\n\n    " + s["title"] + "\n\n" + bar + "\n" + s["body"]
    return out


def after_lowering_text(text):
    """`(After Lowering)` 段的原文；没有这一段返回 None。"""
    return stage_section_texts(text).get("After Lowering")


def var_decl_name(decl):
    """从 Variable.toString 的声明串取名字（NFVariable.mo:527 toStream：前缀 + 类型 + ' ' + cref）。

    类型和 cref 都可能含 `[1, 2]`、`enumeration(a, b)`、`'带空格'`，所以按括号深度 / 引号切词。"""
    toks, cur, depth, quote = [], "", 0, False
    for c in decl:
        if quote:
            cur += c
            quote = c != "'"
        elif c == "'":
            cur += c
            quote = True
        elif c in "[(":
            depth += 1
            cur += c
        elif c in "])":
            depth -= 1
            cur += c
        elif c == " " and depth == 0:
            toks.append(cur)
            cur = ""
            if len(toks) >= 8:      # 名字一定在前几个词里，后面的修饰不必再切
                break
        else:
            cur += c
    if cur:
        toks.append(cur)
    i = 0
    while i < len(toks) and toks[i] in _DECL_WORDS:
        i += 1
    if i + 1 < len(toks) and toks[i + 1] not in ("=", ""):
        return toks[i + 1].rstrip(";")
    return toks[i].rstrip(";") if i < len(toks) else ""      # `$ANY$ `：类型名后面是空名字


def _rows(body):
    return body.replace("\r\n", "\n").split("\n")


def _is_stars(line):
    return len(line) > 2 and set(line) == {"*"}


def parse_var_data(body):
    """VarData（level 2）。返回 {桶名: {"count", "scalar", "items": [{tag, size, decl, name}]}} + 头部标量数。"""
    rows, out, unknowns = _rows(body), {}, None
    cur = None
    for i, line in enumerate(rows):
        if line.startswith("  Equation Data Simulation"):
            break
        m = re.match(r"^  Variable Data Simulation \(scalar unknowns: (\d+)\)$", line)
        if m:
            unknowns = int(m.group(1))
            continue
        b = _VAR_BUCKET.match(line)
        if b and i + 1 < len(rows) and _is_stars(rows[i + 1]):
            cur = {"count": int(b.group(2)), "scalar": int(b.group(3)), "items": []}
            out[b.group(1)] = cur
            continue
        it = _ITEM.match(line)
        if it and cur is not None:
            decl = it.group(4)
            cur["items"].append({"tag": it.group(2), "size": int(it.group(3)), "decl": decl,
                                 "name": var_decl_name(decl)})
        elif cur is not None and cur["items"] and line and not line.startswith("="):
            cur["items"][-1]["decl"] += "\n" + line      # 绑定串里的换行
    return {"scalar_unknowns": unknowns, "buckets": out}


def parse_eq_data(body):
    """EqData（level 1）。返回 {桶名: {"count", "scalar", "items": [{tag, size, text, name}]}} + 头部标量数。

    多行方程的续行带同一序号和 `[----]`（NBEquation.mo:976-986）；`Event Info` 段不归这里。"""
    rows, out, sim = _rows(body), {}, None
    cur, seen = None, False
    for i, line in enumerate(rows):
        m = re.match(r"^  Equation Data Simulation \(scalar simulation equations: (\d+)\)$", line)
        if m:
            sim = int(m.group(1))
            seen = True
            continue
        if not seen:
            continue
        if line == "  Event Info" or line.startswith("  Clock"):
            break
        b = _EQ_BUCKET.match(line)
        if b and i + 1 < len(rows) and _is_stars(rows[i + 1]):
            cur = {"count": int(b.group(2)), "scalar": int(b.group(3)), "items": []}
            out[b.group(1)] = cur
            continue
        c = _CONT.match(line)
        if c and cur is not None and cur["items"]:
            cur["items"][-1]["text"] += "\n" + c.group(2)
            continue
        it = _ITEM.match(line)
        if it and cur is not None:
            n = _RES_NAME.search(it.group(4))
            cur["items"].append({"tag": it.group(2), "size": int(it.group(3)), "text": it.group(4),
                                 "name": n.group(1) if n else ""})
    return {"scalar_equations": sim, "buckets": out}


def parse_event_counts(body):
    """`Event Info` 段（NBEvents.mo:135）里三类事件的行数；没有该段返回 None。"""
    rows, out, cur = _rows(body), None, None
    for i, line in enumerate(rows):
        if line == "  Event Info":
            out = {"time": 0, "composite": 0, "state": 0}
        elif out is not None:
            if i + 1 < len(rows) and _is_stars(rows[i + 1]):
                w = line.split(" ")[0].lower()
                cur = w if w in out else None
            elif cur and line and not _is_stars(line):
                out[cur] += 1
    return out


def parse_blocks(body):
    """分区段（`[ODE] …`）里的分区与块。返回 [{"index", "kind", "blocks": [...]}]。

    块 = {"kind": Single/Multi/Loop…, "linear", "mixed", "vars": [名], "scalar": 标量数}。只认行首带序号的 `BLOCK n:`；
    撕裂集里的内层块用 index = -1 打印（NBTearing.mo:104），算进外层环。"""
    parts, part, blk, mode = [], None, None, None
    for line in _rows(body):
        if line.startswith("  Event Info") or line.startswith("=") and part is None and blk is None:
            continue
        p = _PARTITION.match(line)
        if p:
            part = {"index": int(p.group(1)), "kind": p.group(2), "blocks": []}
            parts.append(part)
            blk, mode = None, None
            continue
        h = _BLOCK_HEAD.match(line)
        if h and part is not None:
            kind = {"Single Strong Component": "Single", "Multi Strong Component": "Multi",
                    "Algebraic Loop": "Loop"}.get(h.group(2), h.group(2))
            blk = {"kind": kind, "vars": [], "scalar": 0}
            if kind == "Loop":
                f = h.group(3) or ""
                blk["linear"] = "Linear = true" in f
                blk["mixed"] = "Mixed = true" in f
            part["blocks"].append(blk)
            mode = None
            continue
        if blk is None:
            continue
        s = line.strip()
        if s.startswith("### "):
            mode = "var" if "Variable" in s else "eqn"
            continue
        if line.startswith("=") or line == "  Event Info":
            blk, mode = None, None
            continue
        if not s or not line.startswith(("\t", " ")):
            continue
        it = re.match(r"^(\[[^\]]+\]) \((-?\d+)\)\s?(.*)$", s.rstrip(";"))
        if mode == "var":
            blk["vars"].append(var_decl_name(it.group(3) if it else s.rstrip(";")))
        elif mode == "eqn" and it and it.group(1) != "[----]":
            blk["scalar"] += int(it.group(2))
    return parts


def project_5a(body, counts=None):
    """D4 的 5a 签名：各 VarData 桶的名字集合、各 EqData 桶的（方程数, 标量数）、前端后计数。"""
    v, e = parse_var_data(body), parse_eq_data(body)
    sig = {"vars": {k: sorted(i["name"] for i in b["items"]) for k, b in v["buckets"].items()},
           "eqs": {k: [b["count"], b["scalar"]] for k, b in e["buckets"].items()}}
    if counts:
        sig["counts"] = {k: list(counts[k]) for k in ("equations", "variables") if k in counts}
    return sig


def first_causalize(sections):
    """第一个 Causalize 模块的全部分区段（主流程；初始化子流程里的同名段归 5d，ADR-008 F2）。

    主流程的 Causalize 紧跟 Partitioning，所以取第一个 module == "Causalize" 的连续一段，遇到 INI 或别的模块即停。"""
    out = []
    for s in sections:
        if s["module"] == "Causalize" and s["tag"] not in (None, "INI", "INI_0"):
            out.append(s)
        elif out:
            break
    return out


def project_5b(sections):
    """D4 的 5b 签名：{段标签: [{"index", "kind", "blocks": [(种类, 变量名集合, 标量数)]}]}。"""
    sig = {}
    for s in first_causalize(sections):
        parts = parse_blocks(s["body"])
        sig[s["tag"]] = [{"index": p["index"], "kind": p["kind"],
                          "blocks": [[b["kind"] + ("(L)" if b.get("linear") else "") + ("(M)" if b.get("mixed") else ""),
                                      sorted(b["vars"]), b["scalar"]] for b in p["blocks"]]}
                         for p in parts]
    return sig


def stage_identity(bodies, counts=None):
    """逐 pre 模块判恒等。返回 {模块: {"text": 段体逐字相同, "sig": 5a 签名相同}}；缺段的模块不出现。

    比的是打印文本：dump 看不见的字段（比如只改了属性指针）这里判不出来。"""
    out = {}
    for prev, cur in zip(PRE_STAGES, PRE_STAGES[1:]):
        if prev in bodies and cur in bodies:
            out[cur] = {"text": bodies[prev] == bodies[cur],
                        "sig": project_5a(bodies[prev]) == project_5a(bodies[cur])}
    return out


# ---------------------------------------------------------------- 自检
_SAMPLE_INFO = """\
Notification: Model statistics after passing the front-end and creating the data structures used by the back-end:
 * Number of equations: 3 (3)
 * Number of variables: 3 (3)
Notification: Partition statistics after passing the back-end:
 * Number of ODE partitions: ..................... 1
 * Number of initial partitions: ................. 1
Notification: Variable statistics after passing the back-end:
 * Number of states: ............................. 1 (1) ('-d=stateselection' for the list of states)
Notification: [Simulation] Strong Component statistics after passing the back-end:
 * Number of single strong components: ........... 2 (scalar:2, array:0, record:0)
 * Number of algebraic-loop strong components: ... 0 (linear: 0, nonlinear:0)
"""

_SAMPLE_BLT = """\
BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)
### Variable:
	Real z
BLOCK 1: Algebraic Loop (Linear = false, Mixed = false)
"""

_SAMPLE_BASE = """\
//! base 0.1.0
package 'P'
  model 'P'
    Real 'x'(start = 1.0);
    Real 'y';
    Real 'z';
  equation
    der('x') = 'y' - 'x';
    'y' = 2.0 * 'z' + 'x';
    'z' = 1.0 - 0.5 * 'y';
  end 'P';
end 'P';
"""


# 真实捕获（omc 1.25.0 `--newBackend -s --simCodeTarget=None -d=optdaedump,bltdump,backenddaeinfo`，ADR-008 附录 A 的 Adr008B）：
# 取 (After Lowering) / (Bindings) / (Events) / 第一个 [ODE] (Causalize) / 第一个 [INI] (Causalize) 五段 + 尾部压平文本。
_SAMPLE_BDAE = """\
####################################

    BackendDAE: (After Lowering)

####################################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real u
(3)       [ALGB] (1) Real v

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Integer n (fixed = true, start = 0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) der(x) = -u ($RES_SIM_3)
(2)       [SCAL] (1) u = v * v + x ($RES_SIM_2)
(3)       [SCAL] (1) v = time - u * u ($RES_SIM_1)

Discrete Equations (1/1)
**************************
(1)       [WHEN] (1) ($RES_SIM_0)
(1)       [----] when x < 0.5 then
(1)       [----]   n := pre(n) + 1
(1)       [----] end when;

##############################

    BackendDAE: (Bindings)

##############################

=================================================
  Variable Data Simulation (scalar unknowns: 4)
=================================================

Algebraic Variables (3/3)
***************************
(1)       [ALGB] (1) Real x (fixed = true, start = 1.0)
(2)       [ALGB] (1) Real u
(3)       [ALGB] (1) Real v

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Integer n (fixed = true, start = 0)

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 4)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) der(x) = -u ($RES_SIM_3)
(2)       [SCAL] (1) u = v * v + x ($RES_SIM_2)
(3)       [SCAL] (1) v = time - u * u ($RES_SIM_1)

Discrete Equations (1/1)
**************************
(1)       [WHEN] (1) ($RES_SIM_0)
(1)       [----] when x < 0.5 then
(1)       [----]   n := pre(n) + 1
(1)       [----] end when;

############################

    BackendDAE: (Events)

############################

=================================================
  Variable Data Simulation (scalar unknowns: 5)
=================================================

State Variables (1/1)
***********************
(1)       [STAT] (1) Real x (fixed = true, start = 1.0)

Derivative Variables (1/1)
****************************
(1)       [DER-] (1) Real $DER.x

Algebraic Variables (2/2)
***************************
(1)       [ALGB] (1) Real u
(2)       [ALGB] (1) Real v

Discrete Variables (1/1)
**************************
(1)       [DISC] (1) Boolean $SEV_0

Discrete State Variables (1/1)
********************************
(1)       [DISS] (1) Integer n (fixed = true, start = 0)

Previous Variables (1/1)
**************************
(1)       [PRE-] (1) Integer $PRE.n

Artificial Variables (2/1)
****************************
(1)       [DMMY] (0) $ANY$ 
(2)       [TIME] (1) Real time


=============================================================
  Equation Data Simulation (scalar simulation equations: 5)
=============================================================

Continuous Equations (3/3)
****************************
(1)       [SCAL] (1) $DER.x = -u ($RES_SIM_3)
(2)       [SCAL] (1) u = v * v + x ($RES_SIM_2)
(3)       [SCAL] (1) v = time - u * u ($RES_SIM_1)

Discrete Equations (2/2)
**************************
(1)       [WHEN] (1) ($RES_SIM_0)
(1)       [----] when $SEV_0 then
(1)       [----]   n := 1 + $PRE.n
(1)       [----] end when;
(2)       [SCAL] (1) $SEV_0 = x < 0.5 ($RES_EVT_4)

(Exclusively) Initial Equations (1/1)
***************************************
(1)       [SCAL] (1) $SEV_0 = x < 0.5 ($RES_EVT_4)

==============
  Event Info
==============

Time Events
*************


Composite Events
******************


State Events
**************
(0) [DISC] (1) Boolean $SEV_0 = x < 0.5

#################################################

    [ODE] Differential-Algebraic: (Causalize)

#################################################

================================
  (1) Continuous ODE Partition
================================

BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Boolean $SEV_0
### Equation:
	[SCAL] (1) $SEV_0 = x < 0.5 ($RES_EVT_4)

BLOCK 2: Multi Strong Component (status = Solve.UNPROCESSED)
--------------------------------------------------------------
### Variables:
	[DISS] (1) Integer n (fixed = true, start = 0)
### Equation:
	[WHEN] (1) ($RES_SIM_0)
	[----] when $SEV_0 then
	[----]   n := 1 + $PRE.n
	[----] end when;

BLOCK 3: Algebraic Loop (Linear = false, Mixed = false)
---------------------------------------------------------
Strict Tearing Set
********************
### Iteration Variables:
	[ALGB] (1) Real u;
	[ALGB] (1) Real v;
### Residual Equations:
	[SCAL] (1) u = v * v + x ($RES_SIM_2);
	[SCAL] (1) v = time - u * u ($RES_SIM_1);
### Inner Equations:
	
BLOCK 4: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Real $DER.x
### Equation:
	[SCAL] (1) $DER.x = -u ($RES_SIM_3)

==============
  Event Info
==============

Time Events
*************


Composite Events
******************


State Events
**************
(0) [DISC] (1) Boolean $SEV_0 = x < 0.5

#########################################

    [INI] Initialization: (Causalize)

#########################################

================================
  (1) Continuous INI Partition
================================

BLOCK 1: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Real x
### Equation:
	[SCAL] (1) x = $START.x ($RES_SRT_5)

BLOCK 2: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Integer $PRE.n
### Equation:
	[SCAL] (1) $PRE.n = $START.n ($RES_SRT_6)

BLOCK 3: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Integer n
### Equation:
	[SCAL] (1) n = $PRE.n ($RES_SRT_7)

BLOCK 4: Algebraic Loop (Linear = false, Mixed = false)
---------------------------------------------------------
Strict Tearing Set
********************
### Iteration Variables:
	[ALGB] (1) Real u;
	[ALGB] (1) Real v;
### Residual Equations:
	[SCAL] (1) u = v * v + x ($RES_SIM_2);
	[SCAL] (1) v = time - u * u ($RES_SIM_1);
### Inner Equations:
	
BLOCK 5: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Real $DER.x
### Equation:
	[SCAL] (1) $DER.x = -u ($RES_SIM_3)

BLOCK 6: Single Strong Component (status = Solve.UNPROCESSED)
---------------------------------------------------------------
### Variable:
	Boolean $SEV_0
### Equation:
	[SCAL] (1) $SEV_0 = x < 0.5 ($RES_EVT_4)

==============
  Event Info
==============

Time Events
*************


Composite Events
******************


State Events
**************
(0) [DISC] (1) Boolean $SEV_0 = x < 0.5

class Adr008B
  Real x(start = 1.0, fixed = true);
  Real u;
  Real v;
  discrete Integer n(start = 0, fixed = true);
equation
  der(x) = -u;
  u = v * v + x;
  v = time - u * u;
  when x < 0.5 then
    n = pre(n) + 1;
  end when;
end Adr008B;
"""


def _selftest():
    n = 0
    cm = parse_checkmodel("Class P has 3 equation(s) and 3 variable(s).\n0 of these are trivial equation(s).")
    assert cm == {"equations": 3, "variables": 3, "trivial": 0}, cm; n += 1
    info = parse_backenddaeinfo(_SAMPLE_INFO)
    assert info["equations"] == (3, 3), info.get("equations"); n += 1
    assert info["states"] == (1, 1), info.get("states"); n += 1
    assert info["ODE partitions"][0] == 1; n += 1
    heads = parse_blt_headers(_SAMPLE_BLT)
    kinds = [h[1] for h in heads]
    assert kinds == ["Single Strong Component", "Algebraic Loop"], kinds; n += 1
    bm = parse_base_modelica(_SAMPLE_BASE)
    assert bm == {"variables": 3, "equations": 3}, bm; n += 1
    cc = compare_counts({"equations": 3, "variables": 3, "states": 1},
                        {"equations": 3, "variables": 3, "states": 2})
    assert cc["states"] == (1, 2, False); n += 1
    # NB 逐模块 dump（期望值读自上面那份实跑文本）
    secs, tail = split_bdae_sections(_SAMPLE_BDAE)
    assert [s["module"] for s in secs] == ["After Lowering", "Bindings", "Events", "Causalize", "Causalize"]; n += 1
    assert [s["tag"] for s in secs] == [None, None, None, "ODE", "INI"]; n += 1
    assert tail.startswith("class Adr008B"), tail[:30]; n += 1
    bodies = pre_stage_bodies(secs)
    lowered = project_5a(bodies["After Lowering"])
    assert lowered["vars"]["Algebraic"] == ["u", "v", "x"] and lowered["vars"]["Discrete"] == ["n"]; n += 1
    assert lowered["eqs"] == {"Continuous": [3, 3], "Discrete": [1, 1]}, lowered["eqs"]; n += 1
    sig = project_5a(bodies["Events"], {"equations": (4, 4), "variables": (4, 4)})
    assert sig["vars"]["State"] == ["x"] and sig["vars"]["Derivative"] == ["$DER.x"]; n += 1
    assert sig["vars"]["Discrete"] == ["$SEV_0"] and sig["vars"]["Discrete State"] == ["n"]; n += 1
    assert sig["vars"]["Previous"] == ["$PRE.n"] and sig["vars"]["Artificial"] == ["$ANY$", "time"]; n += 1
    assert sig["eqs"] == {"Continuous": [3, 3], "Discrete": [2, 2], "(Exclusively) Initial": [1, 1]}, sig["eqs"]; n += 1
    assert sig["counts"] == {"equations": [4, 4], "variables": [4, 4]}; n += 1
    ev = parse_eq_data(bodies["Events"])["buckets"]["Discrete"]["items"]
    assert ev[0]["tag"] == "[WHEN]" and ev[0]["name"] == "$RES_SIM_0" and "n := 1 + $PRE.n" in ev[0]["text"]; n += 1
    assert ev[1]["name"] == "$RES_EVT_4"; n += 1
    assert parse_event_counts(bodies["Events"]) == {"time": 0, "composite": 0, "state": 1}; n += 1
    assert parse_event_counts(bodies["After Lowering"]) is None; n += 1
    ids = stage_identity(bodies)
    assert ids["Bindings"] == {"text": True, "sig": True}, ids; n += 1
    b5 = project_5b(secs)
    assert list(b5) == ["ODE"]; n += 1
    assert b5["ODE"][0]["blocks"] == [["Single", ["$SEV_0"], 1], ["Multi", ["n"], 1], ["Loop", ["u", "v"], 2],
                                      ["Single", ["$DER.x"], 1]], b5; n += 1
    assert var_decl_name("constant enumeration E(one, two) e1 = E.two") == "e1"; n += 1
    assert var_decl_name("parameter Real[3] p = {1.0, 2.0, 3.0} (fixed = true)") == "p"; n += 1
    assert var_decl_name("$ANY$ ") == "$ANY$"; n += 1
    print(f"structcompare self-test: {n} passed")


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        _selftest()
