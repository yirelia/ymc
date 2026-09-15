"""matcompare 自检 + 对拍 OMC diffSimulationResults（FR-011/012 验收）。

用法：
  python tools/test_matcompare.py            # 合成信号单测
  python tools/test_matcompare.py --oracle    # 额外用 omc 生成 mat 对拍（需 omc 在 PATH）

无第三方框架。
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matcompare as mc  # noqa: E402


def unit_tests():
    n = 0
    t = np.linspace(0, 1, 101)
    ref = np.sin(2 * np.pi * t)
    assert mc.cmp_var_tube(t, ref, t, ref, 1e-3, 0.002, 1e-4); n += 1              # 完全相同
    assert not mc.cmp_var_tube(t, ref, t, 1.2 * ref, 1e-3, 0.002, 1e-4); n += 1    # 幅值 +20%
    # 时间平移在 delta=rangeDelta*span=0.002 之内应通过
    small = np.sin(2 * np.pi * (t - 0.0015))
    assert mc.cmp_var_tube(t, ref, t, small, 1e-3, 0.002, 1e-4); n += 1
    # 平移超过 delta 应失败
    big = np.sin(2 * np.pi * (t - 0.02))
    assert not mc.cmp_var_tube(t, ref, t, big, 1e-3, 0.002, 1e-4); n += 1
    # 常量参数：ref=2.0 vs actual=2.0 通过，vs 3.0 失败
    c2 = np.full_like(t, 2.0)
    assert mc.cmp_var_tube(t, c2, t, c2, 1e-3, 0.002, 1e-4); n += 1
    assert not mc.cmp_var_tube(t, c2, t, np.full_like(t, 3.0), 1e-3, 0.002, 1e-4); n += 1
    # rangeDelta=0 必须报错（OMC 会段错误）
    try:
        mc.cmp_var_tube(t, ref, t, ref, 1e-3, 0.0, 1e-4)
        assert False, "rangeDelta=0 应抛错"
    except ValueError:
        n += 1
    # classic：相同通过，差异大失败
    assert mc.cmp_var_classic(t, ref, t, ref, 0.01, 1e-4); n += 1
    assert not mc.cmp_var_classic(t, ref, t, 2.0 * ref, 0.01, 1e-4); n += 1
    print(f"matcompare unit tests: {n} passed")


def _omc():
    from shutil import which
    return which("omc")


def oracle_tests():
    if not _omc():
        print("omc not found; skip oracle 对拍")
        return
    with tempfile.TemporaryDirectory() as d:
        models = {
            "MRef": 2.0, "MTiny": 2.002, "MBig": 3.0,
        }
        lines = []
        for name, k in models.items():
            lines.append(
                f'loadString("model {name} Real x(start=1.0); parameter Real k={k}; '
                f'equation der(x)=-k*x+1.0; end {name};"); getErrorString();')
        for name in models:
            lines.append(f'simulate({name}, stopTime=1.0, numberOfIntervals=50); getErrorString();')
        # diff：actual, expected(ref)
        pairs = [("MRef", "MRef"), ("MTiny", "MRef"), ("MBig", "MRef")]
        for a, b in pairs:
            lines.append(f'diffSimulationResults("{a}_res.mat","{b}_res.mat","d_{a}."); getErrorString();')
        mos = os.path.join(d, "gen.mos")
        with open(mos, "w") as f:
            f.write("\n".join(lines))
        r = subprocess.run(["omc", mos], cwd=d, capture_output=True, text=True, timeout=300)
        verdicts = re.findall(r"\((true|false),\s*\{([^}]*)\}\)", r.stdout)
        if len(verdicts) < 3:
            print("omc 未产出预期 diff 输出，跳过：\n", r.stdout[-500:])
            return
        mism = 0
        for (a, b), (v, varstr) in zip(pairs, verdicts):
            omc_equal = (v == "true")
            ok, fail, _ = mc.compare(os.path.join(d, f"{b}_res.mat"),
                                     os.path.join(d, f"{a}_res.mat"), algo="tube")
            omc_fail = set(x.strip().strip('"') for x in varstr.split(",") if x.strip())
            our_fail = set(fail)
            match = (omc_equal == ok) and (omc_fail == our_fail)
            if not match:
                mism += 1
            print(f"  [{'OK' if match else 'MISMATCH'}] {a} vs {b}: "
                  f"omc=({omc_equal},{sorted(omc_fail)}) ours=({ok},{sorted(our_fail)})")
        if mism:
            print(f"oracle 对拍：{mism} 处不一致")
            sys.exit(1)
        print(f"oracle 对拍：{len(pairs)} 组全部一致")


if __name__ == "__main__":
    unit_tests()
    if "--oracle" in sys.argv:
        oracle_tests()
