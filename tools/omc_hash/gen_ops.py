#!/usr/bin/env python3
"""生成差分用的操作脚本：同一份脚本分别由真容器（mkhsh.py 的 Hsh.mo）和 C++ 容器（hsh_drv.cpp）执行，日志逐行比。

三种脚本：
  hash  L0，裸运行时：短串、溢出 32 位的长串、高位字节、djb2 恰为 0x80000000 的串（labs(LONG_MIN)，中间相遇现算）、超 32 位 / 负的
        Continue 入参、intMod、nextPrime。
  ops   L1，真容器差分（按 --seed 可重放）：跨 rehash 阈值逐个加、删后再加、addNew 造重复、union / union_list（含 extractFromLst 的 size 落后
        一拍）、difference / sym_difference、filter / split / apply / unique_list；Map 的更新、删除再插、merge、subMap、fromLists 重复键、addUpdate；整数键。
  ut    tests/unit/bdae/omc_unordered_test.cpp 里 [容器] 用例的期望值来源：小场景，结果抄进单测。

操作清单（r 是寄存器号，前缀 s = Set<String>、i = Set<Integer>、m = Map<String,Integer>、n = Map<Integer,Integer>，c = Set<cref 文本> 只有 C++ 驱动有）：
  sn r nb | sfl r k… | sa/san/sau r k | sr/sc r k | su r a b | sul r a… | sd/ssd r a b | sfof r a | ssot r1 r2 a | sap r 0|1 | suq k… | sdump r
  mn r nb | mfl r k v… | ma/man/mau/mup r k v | mr/mg/mc r k | mm r a b | msub r a k… | mcp r a | mdump r
  hd s | hc s hi lo | hm hi lo y | hp n | hcr cref（C++ 驱动独有）| # 注释（原样回显）
nb < 0 表示用默认桶数。hc / hm 的入参拆成两段（hi * 2^32 + lo、hi * 2^16 + lo）：omc 的 stringInt 只收 32 位范围。

用法：
  python -I tools/omc_hash/gen_ops.py ops --seed 1 --out ops.txt
  python -I tools/omc_hash/gen_ops.py hash --out ops.txt
  python -I tools/omc_hash/gen_ops.py ut --out ops.txt
一般不用单独跑，diff.py 会调用。
"""
from __future__ import annotations

import argparse
import itertools
import random

MASK = 0xFFFFFFFF


def gen_ops(seed: int) -> bytes:
    rnd = random.Random(seed)
    lines: list[str] = []

    def w(*a) -> None:
        lines.append(" ".join(str(x) for x in a))

    alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$0123456789"

    def name() -> str:
        k = rnd.choice([1, 1, 2, 2, 3, 4, 5, 6, 8, 10, 14, 20, 30])
        return "".join(rnd.choice(alpha) for _ in range(k))

    def names(n: int) -> list[str]:
        seen: list[str] = []
        s = set()
        while len(seen) < n:
            x = name()
            if x not in s:
                s.add(x)
                seen.append(x)
        return seen

    def mkset(r: int, ks: list[str], bk=None) -> None:
        if bk is None:
            w("sfl", r, *ks)
        else:
            w("sn", r, bk)
            for k in ks:
                w("sa", r, k)

    # 1 逐个加，阈值附近每步 dump：13→(第 15 个)29→(31)61→(63)127→(129)257→(259)521
    for t in range(6):
        w("#", "seq", t)
        w("sn", 0, -1)
        ks = names(280)
        for i, k in enumerate(ks, 1):
            w("sa", 0, k)
            if i <= 20 or 28 <= i <= 33 or 60 <= i <= 65 or 126 <= i <= 131 or 256 <= i <= 261 or i % 25 == 0:
                w("sdump", 0)
        w("mn", 0, -1)
        for i, k in enumerate(ks, 1):
            w("ma", 0, k, i)
            if i <= 20 or i % 25 == 0:
                w("mdump", 0)
    # 2 指定初始桶数
    for nb in (1, 2, 3, 5, 7, 11, 13, 17, 100):
        w("#", "init", nb)
        w("sn", 1, nb)
        w("mn", 1, nb)
        ks = names(rnd.choice([3, 10, 40, 120]))
        for k in ks:
            w("sa", 1, k)
            w("ma", 1, k, len(k))
        w("sdump", 1)
        w("mdump", 1)
    # 3 删后再加（Set）：同桶、跨 rehash
    for t in range(30):
        w("#", "remove", t)
        n = rnd.choice([3, 5, 12, 14, 15, 16, 30, 31, 60, 100])
        ks = names(n)
        w("sn", 2, -1)
        for k in ks:
            w("sa", 2, k)
        w("sdump", 2)
        rs = rnd.sample(ks, rnd.randint(1, n))
        for k in rs:
            w("sr", 2, k)
            w("sc", 2, k)
        w("sr", 2, "noSuchKey")
        w("sdump", 2)
        rnd.shuffle(rs)
        for k in rs:
            w("sa", 2, k)
        for k in names(rnd.randint(0, 40)):
            w("sa", 2, k)
        w("sdump", 2)
    # 4 addNew / addUnique：重复键
    for t in range(20):
        w("#", "dup", t)
        ks = names(rnd.choice([3, 6, 14, 20]))
        w("sn", 3, -1)
        for k in ks:
            w("sa", 3, k)
        for k in rnd.sample(ks, len(ks) // 2):
            w("san", 3, k)
        w("sdump", 3)
        for k in rnd.sample(ks, 3):
            w("sau", 3, k)
        w("sau", 3, "freshKey")
        for k in rnd.sample(ks, len(ks) // 3):
            w("sr", 3, k)
        w("sdump", 3)
    # 5 fromList（含重复，桶数按含重复的长度算）
    for t in range(30):
        w("#", "fromList", t)
        ks = names(rnd.choice([0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 20, 50, 120]))
        ks = ks + rnd.sample(ks, len(ks) // 4)
        rnd.shuffle(ks)
        w("sfl", 4, *ks)
        w("sdump", 4)
        for k in names(rnd.randint(0, 30)):
            w("sa", 4, k)
        w("sdump", 4)
    # 6 union：大小关系全覆盖，含相等、共键、别名
    for t in range(120):
        w("#", "union", t)
        pool = names(80)
        a = rnd.sample(pool, rnd.randint(0, rnd.choice([1, 3, 10, 40])))
        b = rnd.sample(pool, rnd.choice([len(a), rnd.randint(0, 40)]))
        mkset(5, a, rnd.choice([None, None, -1]))
        mkset(6, b, rnd.choice([None, None, -1]))
        w("su", 7, 5, 6)
        w("sdump", 7)
        w("sdump", 5)
        w("sdump", 6)
        w("sa", 7, "afterUnion")     # 看别名：7 和 5 / 6 中的一个是同一个对象
        w("sdump", 5)
        w("sdump", 6)
    # 7 union_list：单元素集合堆（邻接矩阵的实际形态）与大小乱序（extractFromLst）
    for t in range(160):
        w("#", "union_list", t)
        n = rnd.randint(0, 9)
        regs = []
        pool = names(120)
        for i in range(n):
            r = 10 + i
            sz = rnd.choice([0, 1, 1, 1, 2, 3, 5, 8, 13, 20, 35])
            mkset(r, rnd.sample(pool, sz), rnd.choice([None, None, -1]))
            regs.append(r)
        w("sul", 8, *regs)
        w("sdump", 8)
    # 8 difference / sym_difference
    for t in range(60):
        w("#", "diff", t)
        pool = names(60)
        a = rnd.sample(pool, rnd.randint(0, 50))
        b = rnd.sample(pool, rnd.randint(0, 50))
        mkset(20, a)
        mkset(21, b)
        w("sd", 22, 20, 21)
        w("sdump", 22)
        w("ssd", 23, 20, 21)
        w("sdump", 23)
    # 9 filter / split / apply / unique_list
    for t in range(60):
        w("#", "misc", t)
        a = names(rnd.choice([0, 1, 5, 13, 14, 15, 30, 50, 100]))
        mkset(24, a, -1)
        w("sfof", 25, 24)
        w("sdump", 25)
        w("ssot", 26, 27, 24)
        w("sdump", 26)
        w("sdump", 27)
        w("sap", 24, rnd.randint(0, 1))
        w("sdump", 24)
        for k in names(rnd.randint(0, 20)):
            w("sa", 24, k)
        w("sdump", 24)
        u = names(rnd.choice([1, 2, 3, 10, 30]))
        u = u + rnd.sample(u, len(u) // 2)
        rnd.shuffle(u)
        if u:
            w("suq", *u)
    # 10 Integer 键（含负数）
    for t in range(80):
        w("#", "int", t)
        n = rnd.choice([1, 3, 10, 14, 15, 31, 70, 140])
        ks = [rnd.randint(-40, 400) for _ in range(n)]
        w("in", 30, rnd.choice([-1, 1, 2, 5, 13, len(ks)]))
        for k in ks:
            w("ia", 30, k)
        w("idump", 30)
        for k in rnd.sample(ks, len(ks) // 3):
            w("ir", 30, k)
        w("idump", 30)
        for k in ks[:len(ks) // 2]:
            w("ia", 30, k)
        w("idump", 30)
        w("ifl", 31, *ks)
        w("idump", 31)
        w("iuq", *ks)
        b = [rnd.randint(-40, 400) for _ in range(rnd.randint(0, 50))]
        w("ifl", 32, *b)
        w("iu", 33, 31, 32)
        w("idump", 33)
        w("iap", 30, rnd.randint(0, 1))
        w("idump", 30)
        w("ifof", 34, 30)
        w("idump", 34)
        w("isot", 35, 36, 30)
        w("idump", 35)
        w("idump", 36)
    # 11 Map：更新、删除、再插、rehash
    for t in range(120):
        w("#", "map", t)
        ks = names(rnd.choice([1, 2, 3, 5, 6, 12, 13, 14, 27, 28, 60, 120]))
        w("mn", 2, rnd.choice([-1, -1, 1, 2, 5, 13, len(ks)]))
        for i, k in enumerate(ks):
            w("ma", 2, k, i)
        w("mdump", 2)
        for k in rnd.sample(ks, len(ks) // 2):
            w("ma", 2, k, rnd.randint(1000, 2000))
        w("mdump", 2)
        rs = rnd.sample(ks, rnd.randint(0, len(ks)))
        for k in rs:
            w("mr", 2, k)
            w("mc", 2, k)
            w("mg", 2, k)
        w("mr", 2, "noSuchKey")
        w("mdump", 2)
        rnd.shuffle(rs)
        for k in rs:
            w("ma", 2, k, rnd.randint(0, 9))
        for k in names(rnd.randint(0, 30)):
            w("ma", 2, k, 1)
        w("mdump", 2)
        for k in rnd.sample(ks, min(5, len(ks))):
            w("mup", 2, k, 7)
        w("mup", 2, "upNew", 3)
        w("mdump", 2)
    # 12 Map：addNew / addUnique / fromLists 重复键 / merge / subMap / copy
    for t in range(100):
        w("#", "map2", t)
        ks = names(rnd.choice([1, 3, 5, 8, 20, 40]))
        w("mn", 3, -1)
        for k in ks:
            w("ma", 3, k, 1)
        for k in rnd.sample(ks, len(ks) // 2):
            w("man", 3, k, 2)
        w("mdump", 3)
        for k in rnd.sample(ks, min(2, len(ks))):
            w("mg", 3, k)
            w("mau", 3, k, 5)
        w("mau", 3, "fresh", 5)
        for k in rnd.sample(ks, len(ks) // 3):
            w("mr", 3, k)
        w("mdump", 3)
        d = ks + rnd.sample(ks, len(ks) // 2)
        rnd.shuffle(d)
        args = []
        for i, k in enumerate(d):
            args += [k, i]
        w("mfl", 4, *args)
        w("mdump", 4)
        a = names(rnd.choice([0, 1, 2, 5, 10, 30]))
        b = [x + "_b" for x in names(rnd.choice([0, 1, 2, 5, 10, 30]))]
        w("mn", 5, rnd.choice([-1, -1, 3]))
        w("mn", 6, rnd.choice([-1, -1, 3]))
        for k in a:
            w("ma", 5, k, 1)
        for k in b:
            w("ma", 6, k, 2)
        w("mm", 7, 5, 6)
        w("mdump", 7)
        if a + b:
            sub = rnd.sample(a + b, rnd.randint(0, len(a + b)))
            w("mcp", 8, 7)
            w("ma", 8, "cpOnly", 1)
            w("mdump", 7)
            w("mdump", 8)
            w("msub", 9, 7, *sub)
            w("mdump", 9)
            w("ma", 9, "subOnly", 1)
            w("mdump", 9)
    # 13 Integer 键 Map
    for t in range(60):
        w("#", "nmap", t)
        ks = [rnd.randint(-20, 300) for _ in range(rnd.choice([1, 3, 8, 20, 60]))]
        w("nn", 10, -1)
        for i, k in enumerate(ks):
            w("na", 10, k, i)
        w("ndump", 10)
        for k in rnd.sample(ks, len(ks) // 3):
            w("nr", 10, k)
        w("ndump", 10)
        for k in ks[:len(ks) // 2]:
            w("nup", 10, k, 1)
        w("ndump", 10)
    return ("\n".join(lines) + "\n").encode("ascii")


def djb2_raw(bs: bytes, h: int = 5381) -> int:
    for c in bs:
        h = (h * 33 + c) & MASK
    return h


def long_min_strings(limit: int = 4) -> list[bytes]:
    """djb2 恰为 0x80000000 的串。33 是奇数，可逆，所以前 3 字符正向建表、后 4 字符从目标反推（中间相遇）。"""
    alpha = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
    inv33 = pow(33, -1, 1 << 32)
    fwd = {djb2_raw(bytes(t)): bytes(t) for t in itertools.product(alpha, repeat=3)}
    found: list[bytes] = []
    for t in itertools.product(alpha, repeat=4):
        h = 0x80000000
        for c in reversed(t):
            h = ((h - c) * inv33) & MASK
        if h in fwd:
            s = fwd[h] + bytes(t)
            assert djb2_raw(s) == 0x80000000
            found.append(s)
            if len(found) >= limit:
                break
    return found


def gen_hash() -> bytes:
    rnd = random.Random(7)
    found = long_min_strings()
    out: list[bytes] = []

    def line(*a) -> None:
        out.append(b" ".join(x if isinstance(x, bytes) else str(x).encode() for x in a))

    def hc(s: bytes, v: int) -> None:
        hi = v >> 32
        lo = v & MASK
        if lo >= (1 << 31):     # lo 也不能超 32 位有符号：拆成 [-2^31, 2^31)
            hi += 1
            lo -= 1 << 32
        line(b"hc", s, hi, lo)

    line(b"#", b"hash")
    for s in [b"x", b"y", b"a", b"$DER", b"$RES_SIM_0", b"$FUN_12", b"time", b"_", b"s", b"uu", b"1", b"12", b"abc", b"zzzz",
              b"abcdefghij", b"ZZZZZZZZZZZZZZZZZZ", b"\xc3\xa9", b"a\xc3\xa9", b"\xe4\xb8\xad\xe6\x96\x87", b"\xff\xfe\xfd"]:
        line(b"hd", s)
    for s in found:
        line(b"hd", s)
    alpha = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_$0123456789"
    for _ in range(600):
        n = rnd.choice([1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 24, 40])
        s = bytes(rnd.choice(alpha) for _ in range(n))
        line(b"hd", s)
        hc(s, rnd.choice([0, 1, 5381, 177693, 2147483647, 2147483648, 2147483649, 4294967295, 4294967296, 4294967297,
                          5000000000, -1, -2147483648, -5, 1234567890123]))
    for s in found:
        hc(b"x", -2147483648)         # 从 LONG_MIN 继续
        hc(s[3:], djb2_raw(s[:3]))    # Continue 自己算出 LONG_MIN
    for a, b in [(-1, 13), (-14, 13), (14, 13), (0, 13), (13, 13), (-13, 13), (-2147483648, 13), (-2147483648, 2),
                 (2147483647, 17), (-1, 1), (5, -3), (-5, -3), (-5, 3), (5, 3)]:
        line(b"hm", a >> 16, a & 0xFFFF, b)
    for n in list(range(-2, 130)) + [255, 256, 257, 1000, 4096, 10007, 65536, 100000]:
        line(b"hp", n)
    return b"\n".join(out) + b"\n"


def gen_ut() -> bytes:
    lines: list[str] = []

    def w(*a) -> None:
        lines.append(" ".join(str(x) for x in a))

    keys = [f"k{i}" for i in range(300)]
    w("#", "thresholds")
    w("sn", 0, -1)
    for i, k in enumerate(keys, 1):
        w("sa", 0, k)
        if i in (13, 14, 15, 29, 30, 31, 61, 62, 63, 127, 128, 129, 257, 258, 259):
            w("sdump", 0)
    w("#", "mapthresholds")
    w("mn", 0, -1)
    for i, k in enumerate(keys[:30], 1):
        w("ma", 0, k, i)
        if i in (1, 2, 3, 5, 6, 7, 13, 14, 15, 29, 30):
            w("mdump", 0)
    w("#", "listarray")
    w("sn", 1, -1)
    for k in "alpha beta gamma delta x y z".split():
        w("sa", 1, k)
    w("sdump", 1)
    w("#", "fromlist")
    for ks in ("", "a", "a b", "a b c", "a b c d", "a a a a a"):
        w("sfl", 2, *ks.split())
        w("sdump", 2)
    w("#", "union-tie")
    w("sfl", 3, "p"); w("sfl", 4, "q"); w("su", 5, 3, 4); w("sdump", 3); w("sdump", 4); w("sdump", 5)
    w("#", "union-left-bigger")
    w("sfl", 3, "p", "r"); w("sfl", 4, "q"); w("su", 5, 3, 4); w("sdump", 3); w("sdump", 4)
    w("#", "union-empty-13")
    w("sn", 3, -1); w("sfl", 4); w("su", 5, 3, 4); w("sdump", 5)
    w("sn", 3, -1); w("sfl", 4); w("su", 5, 4, 3); w("sdump", 5)
    w("#", "union_list-singletons")
    for i, k in enumerate("a b c d e f g h i".split()):
        w("sfl", 10 + i, k)
    w("sul", 6, *range(10, 19)); w("sdump", 6)
    w("#", "union_list-lag")   # 大小 1, 5, 3, 4
    w("sfl", 10, "a"); w("sfl", 11, "b", "c", "d", "e", "f"); w("sfl", 12, "g", "h", "i"); w("sfl", 13, "j", "k", "l", "m")
    w("sul", 6, 10, 11, 12, 13); w("sdump", 6)
    w("#", "difference")
    w("sfl", 3, *"a b c d e f".split()); w("sfl", 4, *"b d x".split())
    w("sd", 5, 3, 4); w("sdump", 5); w("ssd", 5, 3, 4); w("sdump", 5)
    w("#", "remove-readd")
    w("sn", 3, 13)
    for k in "b o c p".split():
        w("sa", 3, k)
    w("sdump", 3); w("sr", 3, "b"); w("sa", 3, "b"); w("sdump", 3); w("sr", 3, "zz"); w("sdump", 3)
    w("#", "addnew-dup")
    w("sn", 3, -1); w("sa", 3, "a"); w("san", 3, "a"); w("sdump", 3); w("sr", 3, "a"); w("sdump", 3); w("sc", 3, "a")
    w("#", "apply")
    w("sn", 3, -1)
    for k in "aa ab ac bd be".split():
        w("sa", 3, k)
    w("sap", 3, 0); w("sdump", 3)
    w("sn", 3, -1)
    for k in "aax aay abx bdy".split():
        w("sa", 3, k)
    w("sap", 3, 1); w("sdump", 3)
    w("#", "filter-split")
    w("sfl", 3, *"a bb ccc dddd eeeee ffffff".split()); w("sfof", 4, 3); w("sdump", 4); w("ssot", 5, 6, 3); w("sdump", 5); w("sdump", 6)
    w("#", "unique")
    w("suq", "z"); w("suq", "b", "a", "b", "c", "a")
    w("iuq", 5, 3, 5, 9, 1)
    w("iuq", 9)
    w("#", "int-negative")
    w("in", 7, 5); w("ia", 7, -1); w("ia", 7, -6); w("ia", 7, 4); w("ia", 7, 9); w("idump", 7)
    w("#", "map-update-remove")
    w("mn", 1, -1)
    for k, v in (("c", 1), ("a", 2), ("b", 3)):
        w("ma", 1, k, v)
    w("ma", 1, "a", 9); w("mdump", 1); w("mr", 1, "c"); w("mdump", 1); w("ma", 1, "c", 4); w("mdump", 1); w("mg", 1, "b"); w("mg", 1, "zz")
    w("#", "map-merge-tie")
    w("mn", 2, -1); w("ma", 2, "a", 1); w("ma", 2, "b", 2)
    w("mn", 3, -1); w("ma", 3, "c", 3); w("ma", 3, "d", 4)
    w("mm", 4, 2, 3); w("mdump", 4)
    w("mn", 3, -1); w("ma", 3, "c", 3)
    w("mm", 4, 2, 3); w("mdump", 4)
    w("#", "map-sub")
    w("msub", 5, 2, "b", "a"); w("mdump", 5)
    w("#", "map-fromlists-dup")
    w("mfl", 6, "x", 1, "y", 2, "x", 3); w("mdump", 6)
    w("#", "map-addnew")
    w("mn", 7, -1); w("ma", 7, "a", 1); w("man", 7, "a", 2); w("mdump", 7); w("mg", 7, "a"); w("mr", 7, "a"); w("mdump", 7); w("mg", 7, "a")
    w("#", "map-addupdate")
    w("mn", 8, -1); w("mup", 8, "a", 5); w("mup", 8, "b", 1); w("mup", 8, "a", 7); w("mdump", 8)
    w("#", "map-zero-buckets")
    # 0 桶的 add 在 OMC 里 fail（loadFactor 除零），日志是 `FAILED ma 9 a 1`；键已 push，但这之后的状态不算语义，脚本到此为止。
    w("mn", 9, 0); w("mc", 9, "a"); w("mg", 9, "a"); w("mdump", 9); w("ma", 9, "a", 1)
    return ("\n".join(lines) + "\n").encode("ascii")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("kind", choices=["ops", "hash", "ut"])
    ap.add_argument("--seed", type=int, default=1, help="ops 的随机种子")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    data = gen_ops(a.seed) if a.kind == "ops" else gen_hash() if a.kind == "hash" else gen_ut()
    with open(a.out, "wb") as f:
        f.write(data)
    print(a.kind, len(data.split(b"\n")) - 1, "行")


if __name__ == "__main__":
    main()
