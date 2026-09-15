"""omc_diff 的自检 + 对拍真实 omc-diff.exe（FR-002 验收）。

用法：
  python tests/omc/test_omc_diff.py            # 纯单测（assert）
  python tests/omc/test_omc_diff.py --oracle   # 额外对拍 omc-diff.exe（需能找到它）

无第三方框架（方案 §3.2）。
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import omc_diff  # noqa: E402


def _eq(exp, act, eps):
    return omc_diff.compare(exp, act, eps).equal


def unit_tests():
    n = 0
    # 整数精确
    assert _eq("x = 10\n", "x = 10\n", 1e-7); n += 1
    assert not _eq("x = 10\n", "x = 11\n", 1e-7); n += 1
    # 实数：1.0000001 vs 1.0，eps=1e-7 通过，更小则失败
    assert _eq("1.0000001", "1.0", 1e-7); n += 1
    assert not _eq("1.0000001", "1.0", 1e-9); n += 1
    # 相对判据用 actual 作分母：|d2-d1| <= eps*|d2|+eps
    assert _eq("1000.0", "1000.05", 5e-3); n += 1
    # 文本空白折叠
    assert _eq("a   b\n", "a b\n", 1e-7); n += 1
    assert _eq("  leading", "leading", 1e-7); n += 1
    assert not _eq("Files Equal!", "Files not Equal!", 1e-7); n += 1
    # 类型不同：整数 token vs 文本
    d = omc_diff.compare("abc\n", "123\n", 1e-7)
    assert not d.equal and d.kind == "token", d; n += 1
    # 长度不同
    assert not _eq("a\nb\n", "a\n", 1e-7); n += 1
    # 行号定位
    d = omc_diff.compare("a\nb\n7.0\n", "a\nb\n9.0\n", 1e-7)
    assert not d.equal and d.line == 3, d; n += 1
    # record 打印样例（helloWorld 风格），无浮点差异
    rec = 'record R x = 1.0, y = "s" end R;\n'
    assert _eq(rec, rec, 5e-3); n += 1
    # 负号数字
    assert _eq("-4.42945", "-4.42945", 1e-7); n += 1
    assert not _eq("-4.42945", "4.42945", 1e-7); n += 1
    print(f"omc_diff unit tests: {n} passed")


def _find_omc_diff():
    cands = [
        r"C:\Program Files\OpenModelica1.25.0-64bit\bin\omc-diff.exe",
        r"C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc-diff.exe",
    ]
    for c in cands:
        if os.path.exists(c):
            return c
    return None


def oracle_tests():
    exe = _find_omc_diff()
    if not exe:
        print("omc-diff.exe not found; skip oracle 对拍")
        return
    cases = [
        ("10\n", "10\n", 1e-7),
        ("10\n", "11\n", 1e-7),
        ("1.0000001\n", "1.0\n", 1e-7),
        ("1.0000001\n", "1.0\n", 1e-9),
        ("a   b\n", "a b\n", 1e-7),
        ("Files Equal!\n", "Files not Equal!\n", 1e-7),
        ("x = 3.14159\ny = 2.0\n", "x = 3.14160\ny = 2.0\n", 5e-3),
        ("val = -4.42945\n", "val = -4.42944\n", 1e-3),
        ("1000.0\n", "1000.05\n", 5e-3),
        ("a\nb\n", "a\n", 1e-7),
    ]
    mism = 0
    for exp, act, eps in cases:
        with tempfile.TemporaryDirectory() as d:
            pe = os.path.join(d, "expected")
            pa = os.path.join(d, "actual")
            with open(pe, "w", newline="") as f:
                f.write(exp)
            with open(pa, "w", newline="") as f:
                f.write(act)
            # omc-diff：argv = eps expected actual；退出码 0=equal
            r = subprocess.run([exe, str(eps), pe, pa], capture_output=True, text=True)
            oracle_equal = (r.returncode == 0)
            ours = omc_diff.compare(exp, act, eps).equal
            tag = "OK" if oracle_equal == ours else "MISMATCH"
            if oracle_equal != ours:
                mism += 1
            print(f"  [{tag}] eps={eps} exp={exp!r} act={act!r} "
                  f"oracle={oracle_equal} ours={ours}")
    if mism:
        print(f"oracle 对拍：{mism} 处不一致")
        sys.exit(1)
    print(f"oracle 对拍：{len(cases)} 组全部一致")


if __name__ == "__main__":
    unit_tests()
    if "--oracle" in sys.argv:
        oracle_tests()
