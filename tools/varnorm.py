"""变量名归一化与交集匹配（FR-014）。

用于跨工具（ymc/OMC/Dymola）对齐变量名。不同工具在数组下标空格、引号、der 写法上
有差异；归一化后取交集比对，未匹配变量单独列出并粗分类。

来源：R3§7（Dymola readTrajectoryNames 用 der(x)/方括号）、R4（OMC 命名）、R5（MAP-LIB
csv 用方括号点分全路径、不含 der()）；用户决定 2/Q6。
"""
from __future__ import annotations

import re


def normalize(name):
    """规范化一个变量名：去引号、去数组下标内空格、统一空白。"""
    s = name.strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1]
    s = re.sub(r"\s*,\s*", ",", s)      # x[1, 2] -> x[1,2]
    s = re.sub(r"\s*\[\s*", "[", s)
    s = re.sub(r"\s*\]\s*", "]", s)
    s = re.sub(r"\s+", " ", s)
    return s


def classify_unmatched(name):
    """给未匹配变量一个粗分类，便于台账。"""
    n = normalize(name)
    if n.startswith("der(") or n.endswith("'"):
        return "der"
    if n.startswith("$") or "$" in n:
        return "generated"          # $cse/$DER/$RES 等生成变量
    if n.lower() in ("time",):
        return "time"
    return "naming-or-protected"


def match(names_a, names_b):
    """返回 (common, only_a, only_b)，均为归一化后的名字集合（保留原名映射用 map_*）。"""
    na = {normalize(x): x for x in names_a}
    nb = {normalize(x): x for x in names_b}
    ka, kb = set(na), set(nb)
    common = sorted(ka & kb)
    only_a = sorted(ka - kb)
    only_b = sorted(kb - ka)
    return common, only_a, only_b


def _selftest():
    n = 0
    assert normalize('"x"') == "x"; n += 1
    assert normalize("x[1, 2]") == "x[1,2]"; n += 1
    assert normalize("r.a") == "r.a"; n += 1
    assert normalize("der(x)") == "der(x)"; n += 1
    common, oa, ob = match(["time", "x", "der(x)", "k"], ["time", "x", "y"])
    assert common == ["time", "x"], common; n += 1
    assert oa == ["der(x)", "k"] and ob == ["y"], (oa, ob); n += 1
    assert classify_unmatched("der(x)") == "der"; n += 1
    assert classify_unmatched("$cse1") == "generated"; n += 1
    print(f"varnorm self-test: {n} passed")


if __name__ == "__main__":
    _selftest()
