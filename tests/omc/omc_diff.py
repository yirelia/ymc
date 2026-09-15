"""omc-diff 复刻：OMC testsuite 的词法级模糊文本比对（FR-002）。

忠实移植 testsuite/difftool/omc-diff.l（v1.4）的语义：
- 把两侧文本切成 T_REAL / T_TEXT / T_NEWLINE 三种 token（flex 最长匹配）。
- token 类型不同 → 失败。
- 两侧都是纯整数 → 整数精确比较。
- 否则按实数比：|actual-expected| > eps*|actual| + eps 才算失败（注意分母用 actual，
  对应 omc-diff.l:127 `err > eps*fabs(d2)+eps`，d2=actual）。
- 文本 token 折叠连续空白后逐字符比较（strcmp_omc_diff）。

默认 eps：.mo 用 1e-7，.mos 用 5e-3（rtest:105-106）。

来源：testsuite/difftool/omc-diff.l:9-14（词法）、:48-68（strcmp_omc_diff）、
:96-133（比较循环）；研究底稿 R2§B。

这是基建工具（非编译器核心），由 Claude 提供。
"""
from __future__ import annotations

import re
from dataclasses import dataclass

# 与 omc-diff.l 一致的默认容差
EPS_MO = 1e-7
EPS_MOS = 5e-3

# flex 词法规则（omc-diff.l:9-14）。letter = [^0-9\-\n\r]，故空格/制表符属于 letter。
_NEWLINE = re.compile(r"\r\n|\n\r|\n|\r")
_REAL = re.compile(r"-?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?")
_TEXT = re.compile(r"-?[^0-9\-\n\r]+")
_PURE_INT = re.compile(r"-?[0-9]+\Z")

T_NEWLINE = "NEWLINE"
T_REAL = "REAL"
T_TEXT = "TEXT"


@dataclass
class Diff:
    equal: bool
    line: int = 0
    kind: str = ""          # 'token' | 'integer' | 'real' | 'text' | 'length'
    expected: str = ""
    actual: str = ""
    message: str = ""


def tokenize(text: str):
    """按 flex 最长匹配把文本切成 (type, lexeme) 列表；无法匹配的单字符跳过
    （对应 flex 默认 ECHO 规则：字符被消费但不产生 token）。"""
    toks = []
    i, n = 0, len(text)
    while i < n:
        m = _NEWLINE.match(text, i)
        if m:
            toks.append((T_NEWLINE, m.group()))
            i = m.end()
            continue
        m = _REAL.match(text, i)
        if m:
            toks.append((T_REAL, m.group()))
            i = m.end()
            continue
        m = _TEXT.match(text, i)
        if m:
            toks.append((T_TEXT, m.group()))
            i = m.end()
            continue
        i += 1  # 未匹配字符（如孤立 '-'）：跳过
    return toks


def _fold_ws(s: str) -> str:
    return re.sub(r"[ \t]+", " ", s).strip()


def compare(expected: str, actual: str, eps: float) -> Diff:
    """比较 expected 与 actual 文本。返回 Diff（equal + 首个差异定位）。

    line 从 1 起，每遇到一个 NEWLINE token 自增（与 omc-diff.l 的行计数一致）。
    """
    te = tokenize(expected)
    ta = tokenize(actual)
    line = 1
    i = 0
    ne, na = len(te), len(ta)
    while i < ne and i < na:
        (typ_e, lex_e) = te[i]
        (typ_a, lex_a) = ta[i]
        if typ_e != typ_a:
            return Diff(False, line, "token", lex_e, lex_a,
                        f"Line {line}: token type differs ({typ_e} vs {typ_a})")
        if typ_e == T_NEWLINE:
            line += 1
        elif typ_e == T_TEXT:
            if _fold_ws(lex_e) != _fold_ws(lex_a):
                return Diff(False, line, "text", lex_e, lex_a,
                            f"Line {line}: text differs")
        else:  # T_REAL
            if _PURE_INT.match(lex_e) and _PURE_INT.match(lex_a):
                if int(lex_e) != int(lex_a):
                    return Diff(False, line, "integer", lex_e, lex_a,
                                f"Line {line}: integer {lex_e} != {lex_a}")
            else:
                d1 = float(lex_e)   # expected
                d2 = float(lex_a)   # actual
                if abs(d2 - d1) > eps * abs(d2) + eps:
                    return Diff(False, line, "real", lex_e, lex_a,
                                f"Line {line}: real {lex_e} != {lex_a} "
                                f"(eps={eps}, diff={abs(d2 - d1):.3g})")
        i += 1
    if ne != na:
        # 一侧比另一侧长
        who = "actual" if na > ne else "expected"
        return Diff(False, line, "length", "", "",
                    f"Line {line}: {who} file has more input")
    return Diff(True, line)


def compare_files(expected_path: str, actual_path: str, eps: float) -> Diff:
    with open(expected_path, "r", encoding="utf-8", errors="replace") as f:
        exp = f.read()
    with open(actual_path, "r", encoding="utf-8", errors="replace") as f:
        act = f.read()
    return compare(exp, act, eps)


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(description="omc-diff 复刻（token 级模糊比对）")
    p.add_argument("eps", type=float)
    p.add_argument("expected")
    p.add_argument("actual")
    args = p.parse_args(argv)
    d = compare_files(args.expected, args.actual, args.eps)
    if d.equal:
        return 0
    print(d.message)
    print(f"expected: {d.expected}")
    print(f"got:      {d.actual}")
    return 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
