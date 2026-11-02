#!/usr/bin/env python3
"""L0 / L1 差分：同一份操作脚本分别跑真容器（omc 编译 v1.25.0 原文，见 mkhsh.py）和 C++ 容器（hsh_drv.cpp），日志逐行比。

  L0 --hash      裸运行时的整数值：stringHashDjb2、Continue、intMod、nextPrime
  L1 --seed N    真容器差分：Set / Map 的全部操作，dump 里带桶数和两种遍历序；可给多个种子
     --ut        tests/unit/bdae/omc_unordered_test.cpp 的 [容器] 期望值来源

比较时只统一 CRLF（C++ 的 stdout 是文本模式）。oracle 只用 PATH 上的安装版 omc 1.25.0，每次带超时（要现场编译 C，放宽到 280 秒）。

用法：
  cmake --build <构建目录> -j 3 --target omc_hash_drv
  python -I tools/omc_hash/diff.py --work <工作目录> --drv <构建目录>/tests/omc_hash_drv --hash --seed 1 [--seed 2 …] [--ut]
        [--omc-src C:/dev/OpenModelica]
退出码：全部一致 0，有不一致 1。工作目录里留 omc.<名字>.log / cpp.<名字>.log。
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_ops  # noqa: E402
import mkhsh  # noqa: E402

OMC_TIMEOUT = 280


def strip_echo(raw: bytes) -> list[str]:
    """剥掉 omc 脚本的回显，只留 Hsh.run 打印的日志：从第一个 `# ` 行起，到末尾的 0 / "" 回显之前。"""
    lines = raw.replace(b"\r\n", b"\n").decode("latin-1").split("\n")
    i = 0
    while i < len(lines) and not lines[i].startswith("# "):
        i += 1
    j = len(lines)
    while j > i and lines[j - 1].strip() in ("", '""', "0"):
        j -= 1
    return lines[i:j]


def run_case(name: str, ops: bytes, work: str, drv: str) -> int:
    with open(os.path.join(work, "ops.txt"), "wb") as f:
        f.write(ops)
    try:
        r = subprocess.run(["omc", "run.mos"], cwd=work, capture_output=True, timeout=OMC_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"{name}：omc 超时（{OMC_TIMEOUT} 秒）")
    want = strip_echo(r.stdout)
    if not want:
        raise SystemExit(f"{name}：omc 没有输出 rc={r.returncode}\n{r.stdout[-1000:]!r}\n{r.stderr[-1000:]!r}")
    c = subprocess.run([drv, os.path.join(work, "ops.txt")], capture_output=True, timeout=OMC_TIMEOUT)
    got = c.stdout.replace(b"\r\n", b"\n").decode("latin-1").split("\n")
    if got and got[-1] == "":
        got.pop()
    with open(os.path.join(work, f"omc.{name}.log"), "w", encoding="latin-1", newline="\n") as f:
        f.write("\n".join(want) + "\n")
    with open(os.path.join(work, f"cpp.{name}.log"), "w", encoding="latin-1", newline="\n") as f:
        f.write("\n".join(got) + "\n")

    differ = abs(len(want) - len(got))
    values = dumps = items = failed = 0
    for i, (x, y) in enumerate(zip(want, got)):
        if x != y:
            differ += 1
            if differ <= 6:
                print(f"  行 {i + 1}\n    omc {x[:300]}\n    cpp {y[:300]}")
            continue
        if x.startswith(("hd ", "hc ", "hm ", "hp ")):
            values += 1
        elif " L=" in x or " K=" in x:
            dumps += 1
            items += x.count(",") + 2          # 元素个数的近似，加桶数
    failed = sum(1 for x in want if x.startswith(("UNKNOWN", "FAILED")))
    print(f"{name}: 行 {len(want)} / {len(got)}，裸值 {values}，dump {dumps}（约 {items} 项），不一致 {differ}，omc 侧 FAILED/UNKNOWN {failed}，驱动退出码 {c.returncode}")
    return differ + (1 if c.returncode != 0 else 0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", required=True, help="工作目录（omc 会在这里生成一堆 C 文件和 DLL）")
    ap.add_argument("--drv", required=True, help="omc_hash_drv 可执行文件")
    ap.add_argument("--omc-src", default=mkhsh.DEFAULT_OMC_SRC, help="OpenModelica 源码仓库，固定读 tag " + mkhsh.TAG)
    ap.add_argument("--hash", action="store_true", help="L0")
    ap.add_argument("--seed", type=int, action="append", default=[], help="L1 的种子，可重复")
    ap.add_argument("--ut", action="store_true", help="单测期望值那份小脚本")
    a = ap.parse_args()
    mkhsh.check_omc()
    mkhsh.prepare(a.work, a.omc_src)
    drv = os.path.abspath(a.drv)
    bad = 0
    if a.hash:
        bad += run_case("hash", gen_ops.gen_hash(), a.work, drv)
    for seed in a.seed:
        bad += run_case(str(seed), gen_ops.gen_ops(seed), a.work, drv)
    if a.ut:
        bad += run_case("ut", gen_ops.gen_ut(), a.work, drv)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
