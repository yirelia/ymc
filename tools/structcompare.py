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
    print(f"structcompare self-test: {n} passed")


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        _selftest()
