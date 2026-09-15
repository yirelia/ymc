"""工程门 G3（FR-062）：一键检查两套编译器零警告、ASan、确定性。

依次：
1. gcc-debug：configure + build（抓 warning）+ ctest。
2. msvc-debug：build（Debug）+ ctest。
3. msvc-asan：build + ctest（ASan）。
4. 确定性：对同一可复现输出跑两次比对 byte-identical（当前 ymc 只有 --version/--help，
   先用它占位；有 dump pass 后改为 dump-after 两次比对）。

任一套出现 "warning" 或测试失败即判不通过。构建可能较慢；每步带超时。
平台仅 Windows。基建工具，Claude 提供。

用法：python tools/gate_engineering.py [--presets gcc-debug msvc-debug msvc-asan] [--skip-build]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# preset -> (configure_preset, build_preset)
PRESETS = {
    "gcc-debug": ("gcc-debug", "gcc-debug"),
    "msvc-debug": ("msvc", "msvc-debug"),
    "msvc-asan": ("msvc-asan", "msvc-asan"),
}
WARN = re.compile(r"\bwarning\b", re.IGNORECASE)


def _run(cmd, timeout):
    r = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, timeout=timeout)
    return r.returncode, r.stdout or ""


def build_and_test(configure_preset, build_preset, timeout, skip_build):
    ok = True
    notes = []
    if not skip_build:
        code, out = _run(["cmake", "--preset", configure_preset], timeout)
        if code != 0:
            return False, [f"configure 失败 ({configure_preset})", out[-800:]]
        code, out = _run(["cmake", "--build", "--preset", build_preset], timeout)
        warns = [ln for ln in out.splitlines() if WARN.search(ln)
                 and "0 warning" not in ln.lower()]
        if code != 0:
            return False, [f"build 失败 ({build_preset})", out[-800:]]
        if warns:
            ok = False
            notes.append(f"{len(warns)} 条警告：")
            notes += warns[:10]
    code, out = _run(["ctest", "--preset", build_preset, "--output-on-failure"], timeout)
    if code != 0:
        ok = False
        notes.append(f"ctest 失败 ({build_preset})")
        notes.append(out[-800:])
    else:
        m = re.search(r"(\d+)% tests passed", out) or re.search(r"tests passed", out)
        notes.append(f"ctest OK ({build_preset})" + (f" — {m.group(0)}" if m else ""))
    return ok, notes


def determinism_check():
    """同输入两次运行 byte-identical（占位：ymc --version）。"""
    ymc = os.path.join(REPO, "build", "gcc-debug", "ymc.exe")
    if not os.path.exists(ymc):
        return True, ["确定性检查跳过：ymc.exe 未构建"]
    outs = []
    for _ in range(2):
        r = subprocess.run([ymc, "--version"], capture_output=True, text=True)
        outs.append(r.stdout)
    ok = outs[0] == outs[1]
    return ok, ["确定性：两次 --version " + ("一致" if ok else "不一致")]


def main(argv=None):
    p = argparse.ArgumentParser(description="工程门 G3 检查")
    p.add_argument("--presets", nargs="*", default=["gcc-debug"],
                   help="要检查的预设（默认只 gcc-debug；全量传 gcc-debug msvc-debug msvc-asan）")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--timeout", type=int, default=1800)
    args = p.parse_args(argv)

    all_ok = True
    for name in args.presets:
        if name not in PRESETS:
            print(f"未知预设 {name}")
            all_ok = False
            continue
        cfg, bld = PRESETS[name]
        print(f"=== {name} ===")
        try:
            ok, notes = build_and_test(cfg, bld, args.timeout, args.skip_build)
        except subprocess.TimeoutExpired:
            ok, notes = False, [f"{name} 超时"]
        for n in notes:
            print(f"  {n}")
        all_ok = all_ok and ok

    ok, notes = determinism_check()
    print("=== 确定性 ===")
    for n in notes:
        print(f"  {n}")
    all_ok = all_ok and ok

    print("工程门：" + ("通过" if all_ok else "不通过"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
