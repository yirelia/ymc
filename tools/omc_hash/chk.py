#!/usr/bin/env python3
"""L2 对拍：C++ 容器（经 hsh_drv）的预测 vs omc 1.25.0 NB 实跑输出。

用法：
  cmake --build <构建目录> -j 3 --target omc_hash_drv
  python -I tools/omc_hash/chk.py --drv <构建目录>/tests/omc_hash_drv --run-dir <目录> --probes tests/probes/bdae/s5b0.probes
  python -I tools/omc_hash/chk.py --drv <…> --run-dir <目录> --list corpus.txt       # 每行：名字<TAB>model.mo 路径[<TAB>额外 omc 参数…]
omc 的输出缓存在 <run-dir>/<模型>/out.txt，已有就不重跑。不一致写到 <run-dir>/fails.json，并按表达式形状分组打印。退出码：全部一致 0，否则 1。

每个模型跑 `omc --newBackend -s --simCodeTarget=None -d=optdaedump,bltmatrixdump,bltdump model.mo`（PATH 上的安装版，带超时）。比较项：
  mono    分区未知量 / 方程名：cref_hash % B 单调不增（B 由元素数按 Set 规则算）。不依赖插入序，语料也能跑。
  order   分区未知量 / 方程的全序：插入序取自 (Events) 段，C++ Set 逐个 add 再 to_list。只比插入序可推的分区：未知量都是 Algebraic / Derivative，
          方程都是 Continuous。含离散变量或 $RES_EVT 方程的分区只有 mono。
  row     Full 邻接行：把方程文本解析成表达式树，按 collectDependencies 的 union 树喂 C++ Set（见 Emit）。只比 [SCAL] 方程，WHEN / IF / ALGORITHM 和解析器
          不支持的表达式计入 row-skip。
  when    when 拆分序（只对 HshW* 探针，见 check_when）：split 内部序 = keyList + toList(state_set)。
  print   when 的打印序 = split 内部序再反一次（同上）。

S5b-1 要复用 union 树的话看 Emit 和 scalar_row_ops：它们是 collectDependencies 的已验证参考，只覆盖 SCALAR 方程。
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mkhsh  # noqa: E402

FLAGS = ["--newBackend", "-s", "--simCodeTarget=None", "-d=optdaedump,bltmatrixdump,bltdump"]
OMC_TIMEOUT = 120
DRV_REGISTERS = 4000     # hsh_drv.cpp 的 kRegisters 留点余量


def run_omc(job: tuple) -> str:
    name, src, cwd, extra = job
    out = os.path.join(cwd, "out.txt")
    if os.path.exists(out):
        return name
    os.makedirs(cwd, exist_ok=True)
    if src is not None:
        with open(os.path.join(cwd, "model.mo"), "w", encoding="utf-8", newline="\n") as f:
            f.write(src)
    try:
        r = subprocess.run(["omc"] + FLAGS + extra + ["model.mo"], cwd=cwd, capture_output=True, timeout=OMC_TIMEOUT)
        stdout, rc = r.stdout, r.returncode
    except subprocess.TimeoutExpired:
        stdout, rc = b"", -9
    with open(out, "wb") as f:
        f.write(stdout)
    with open(os.path.join(cwd, "rc.txt"), "w", encoding="ascii") as f:
        f.write(str(rc))
    return name


def set_buckets(n: int) -> int:
    """逐个 add n 个不同元素后的桶数（UnorderedSet.mo addKey + rehash）。只用来算 mono 的 B。"""
    def next_prime(m: int) -> int:
        if m <= 2:
            return 2
        p = m + ((m + 1) % 2)
        while True:
            i, q, prime = 3, p // 3, True
            while q >= i:
                if p == q * i:
                    prime = False
                    break
                i += 2
                q = p // i
            if prime:
                return p
            p += 2

    b, size = 13, 0
    for _ in range(n):
        if size > b:
            b = next_prime(size * 2)
        size += 1
    return b


# ---- 解析 omc 的 dump ----

HDR = re.compile(r"^#{5,}$")
ROW = re.compile(r"^\((\d+)\)\s+\[([A-Z\-]+)\] \((\d+)\) (.*)$")
FULLROW = re.compile(r"^(\S+)\s*\|\s*(\{[^}]*\}|)\s*\.*(\S+)\s+\.*\s*\{(.*)\}\s*$")


def split_sections(lines: list[str]) -> list[list]:
    """[标题, 起, 止]：标题行夹在两行 ### 之间（headline_1）。"""
    idx = [i for i, l in enumerate(lines) if HDR.match(l)]
    out: list[list] = []
    k = 0
    while k + 1 < len(idx):
        a, b = idx[k], idx[k + 1]
        if b - a <= 4:
            out.append([" ".join(lines[a + 1:b]).strip(), b + 1, None])
            k += 2
        else:
            k += 1
    for s in out:
        pos = idx.index(s[1] - 1) + 1
        s[2] = idx[pos] if pos < len(idx) else len(lines)
    return out


def decl_name(rest: str) -> str:
    """`discrete Real d2`、`Real[2, 3] y`、`Boolean c = time > 0.5`、`Real x (fixed = true)` → 名字。"""
    toks: list[str] = []
    cur = ""
    depth = 0
    for ch in rest:
        if ch == "[":
            depth += 1
        if ch == "]":
            depth -= 1
        if ch == " " and depth == 0:
            if cur:
                toks.append(cur)
            cur = ""
        else:
            cur += ch
    if cur:
        toks.append(cur)
    keep: list[str] = []
    for t in toks:
        if t == "=" or t.startswith("("):
            break
        keep.append(t)
    return keep[-1]


def parse_var_block(lines: list[str], i: int) -> tuple[list[str], int]:
    """i 指向标题下面的 *** 行。"""
    names: list[str] = []
    i += 1
    while i < len(lines) and lines[i].startswith("("):
        m = ROW.match(lines[i])
        if m:
            names.append(decl_name(m.group(4)))
        i += 1
    return names, i


def parse_eq_block(lines: list[str], i: int) -> tuple[list[dict], int]:
    eqs: list[dict] = []
    i += 1
    while i < len(lines) and lines[i].startswith("("):
        m = ROW.match(lines[i])
        if m and m.group(2) != "----":
            txt = m.group(4)
            mm = re.search(r"\((\$[^()\s]*)\)\s*$", txt)
            eqs.append({"tag": m.group(2), "text": txt[:mm.start()].strip() if mm else txt, "name": mm.group(1) if mm else None})
        i += 1
    return eqs, i


def parse_partitions(lines: list[str], a: int, b: int) -> list[dict]:
    parts: list[dict] = []
    cur = None
    i = a
    while i < b:
        l = lines[i]
        m = re.match(r"^\s+\((\d+)\) (.*) Partition\s*$", l)
        if m:
            cur = {"kind": m.group(2), "vars": None, "eqs": None}
            parts.append(cur)
        elif l.startswith("Unknown Variables (") and cur is not None:
            cur["vars"], i = parse_var_block(lines, i + 1)
            continue
        elif l.startswith("Equations Equations (") and cur is not None:
            cur["eqs"], i = parse_eq_block(lines, i + 1)
            continue
        i += 1
    return parts


def parse_vardata(lines: list[str], a: int, b: int) -> dict:
    d: dict = {}
    i = a
    while i < b:
        m = re.match(r"^(.*?) (Variables|Equations|States) \((\d+)/(\d+)\)\s*$", lines[i])
        if m:
            key = (m.group(1) + " " + m.group(2)).strip()
            if m.group(2) == "Equations":
                d[key], i = parse_eq_block(lines, i + 1)
            else:
                d[key], i = parse_var_block(lines, i + 1)
            continue
        i += 1
    return d


def split_top(s: str) -> list[str]:
    out: list[str] = []
    cur = ""
    depth = 0
    i = 0
    while i < len(s):
        c = s[i]
        if c in "[{(":
            depth += 1
        if c in "]})":
            depth -= 1
        if c == "," and depth == 0 and s[i:i + 2] == ", ":
            out.append(cur)
            cur = ""
            i += 2
            continue
        cur += c
        i += 1
    if cur:
        out.append(cur)
    return out


def strip_dep(e: str) -> str:
    """Full 行的元素 `name[LC|{}+]` → name。"""
    depth = 0
    for i in range(len(e) - 1, -1, -1):
        if e[i] == "]":
            depth += 1
        if e[i] == "[":
            depth -= 1
            if depth == 0:
                return e[:i]
    return e


def parse_full(lines: list[str], a: int, b: int) -> list[dict]:
    """(Partitioning) 与第一个 (Causalize) 之间，依次是每个分区的 createFull。"""
    rows: list[dict] = []
    i = a
    while i < b:
        if lines[i].strip() == "FullAdjacencyMatrix":
            i += 3
            cur: dict = {}
            while i < b and lines[i].strip():
                m = FULLROW.match(lines[i])
                if m:
                    cur[m.group(3)] = [strip_dep(x) for x in split_top(m.group(4))]
                i += 1
            rows.append(cur)
        i += 1
    return rows


# ---- 表达式：打印文本 → 树 ----

TOK = re.compile(r'''\s*(?:(?P<num>\d+\.\d*(?:[eE][-+]?\d+)?|\d+(?:[eE][-+]?\d+)?)|(?P<id>\$?[A-Za-z_][\w$]*(?:\[[^\]]*\])*(?:\.\$?[A-Za-z_][\w$]*(?:\[[^\]]*\])*)*)|(?P<op><=|>=|==|<>|:=|[-+*/^(),<>{}])|(?P<str>"[^"]*"))''')


class Unsup(Exception):
    """解析器不支持：这一行计入 row-skip，不算不一致。"""


def tokenize(s: str) -> list[tuple[str, str]]:
    i = 0
    out: list[tuple[str, str]] = []
    while i < len(s):
        m = TOK.match(s, i)
        if not m or m.end() == i:
            if s[i:].strip() == "":
                break
            raise Unsup("tok " + s[i:i + 20])
        out.append((m.lastgroup, m.group(m.lastgroup)))
        i = m.end()
    return out


class Parser:
    """打印文本的递归下降解析。树的节点：
       ('lit',) ('cref', 名字) ('paren', x) ('un', x) ('bin', a, b) ('rel', a, b) ('lbin', a, b)
       ('multi', [项…], 运算符组) ('call', 函数名, [实参…]) ('if', 条件, then, else)
    """

    def __init__(self, toks: list[tuple[str, str]]):
        self.t = toks
        self.i = 0

    def peek(self):
        return self.t[self.i][1] if self.i < len(self.t) else None

    def kind(self):
        return self.t[self.i][0] if self.i < len(self.t) else None

    def next(self):
        x = self.t[self.i]
        self.i += 1
        return x

    def expect(self, v: str) -> None:
        if self.peek() != v:
            raise Unsup("expect " + v)
        self.i += 1

    def expr(self):
        if self.peek() == "if":
            self.next()
            c = self.expr()
            self.expect("then")
            t = self.expr()
            self.expect("else")
            return ("if", c, t, self.expr())
        return self.lor()

    def lor(self):
        a = self.land()
        while self.peek() == "or":
            self.next()
            a = ("lbin", a, self.land())
        return a

    def land(self):
        a = self.lnot()
        while self.peek() == "and":
            self.next()
            a = ("lbin", a, self.lnot())
        return a

    def lnot(self):
        if self.peek() == "not":
            self.next()
            return ("un", self.lnot())
        return self.rel()

    def rel(self):
        a = self.arith()
        if self.peek() in ("<", ">", "<=", ">=", "==", "<>"):
            self.next()
            return ("rel", a, self.arith())
        return a

    def chain(self, sub, ops):
        terms = [sub()]
        inv = False
        while self.peek() in ops:
            if self.peek() in ("-", "/"):
                inv = True
            self.next()
            terms.append(sub())
        if len(terms) == 1:
            return terms[0]
        # combineBinaries：+ - * / 全进 MULTARY，两项也是（src/bdae/combine_binaries.cpp case 3.0 / 4.0）；只有幂留 BINARY。
        # 带反项的 MULTARY 打成 `(a + b) - (c + d)`（NFExpression.mo:2155 multaryString，多于一项才加括号），括号里是同一层的 args / inv_args，要摊平。
        if inv:
            flat = []
            for t in terms:
                if t[0] == "paren" and t[1][0] == "multi" and t[1][2] == ops:
                    flat += t[1][1]
                else:
                    flat.append(t)
            terms = flat
        return ("multi", terms, ops)

    def arith(self):
        if self.peek() == "-":
            self.next()
            return ("un", self.chain(self.term, ("+", "-")))
        return self.chain(self.term, ("+", "-"))

    def term(self):
        return self.chain(self.power, ("*", "/"))

    def power(self):
        a = self.unary()
        if self.peek() == "^":
            self.next()
            return ("bin", a, self.unary())
        return a

    def unary(self):
        if self.peek() == "-":
            self.next()
            return ("un", self.unary())
        return self.atom()

    def atom(self):
        k = self.kind()
        v = self.peek()
        if k in ("num", "str") or v in ("true", "false"):
            self.next()
            return ("lit",)
        if v == "(":
            self.next()
            e = self.expr()
            self.expect(")")
            return ("paren", e)
        if k == "id":
            self.next()
            if self.peek() != "(":
                return ("cref", v)
            self.next()
            args = []
            if self.peek() != ")":
                args.append(self.expr())
                while self.peek() == ",":
                    self.next()
                    args.append(self.expr())
            self.expect(")")
            if v in ("der", "pre", "sample", "edge", "change", "initial", "terminal"):
                raise Unsup("call " + v)
            return ("call", v, args)
        raise Unsup(f"atom {v!r}")


def parse_expr(txt: str):
    p = Parser(tokenize(txt))
    e = p.expr()
    if p.i != len(p.t):
        raise Unsup("trailing")
    return e


def norm(n: str) -> str:
    """dump 里下标是 `y[2, 3]`，驱动的键没有空格。"""
    return n.replace(", ", ",")


def strip_subs(n: str) -> str:
    return re.sub(r"\[[^\]]*\]", "", n)


class Emit:
    """把表达式树翻成 hsh_drv 的操作序列，对应 collectDependencies（NBAdjacency.mo:1651-1852）。序全在这几条形状上：
      - CREF：未知量是 fromList({cref})（2 桶）；不是未知量（time、参数、状态）也是 fromList({})（2 桶），:1674、:1854-1890
      - 字面量等落到 else：new()，13 桶空集，:1850。和上一条的空集在平局时结果不同
      - BINARY / RELATION / LBINARY：union(a, b)，:1707-1720、:1745-1758
      - MULTARY / CALL：sets 头插，所以 union_list 的入参是实参的逆序，:1723-1742、:1811-1818
      - IF：union_list({condition, then, else})，条件最后才算，:1779-1789
      - UNARY / CAST：透传，:1761-1765
    relevant：分区未知量的名字（已去下标）。NB 的名→下标表走 hashStrip，`x[1]`、`x[2]` 都算 `x`。
    """

    def __init__(self, relevant: set, ops: list[str]):
        self.rel = relevant
        self.ops = ops
        self.n = 0

    def new(self) -> int:
        self.n += 1
        if self.n >= DRV_REGISTERS:
            raise Unsup("regs")
        return self.n

    def emit(self, t) -> int:
        k = t[0]
        if k == "paren" or k == "un":
            return self.emit(t[1])
        if k == "lit":
            r = self.new()
            self.ops.append(f"cn {r} -1")
            return r
        if k == "cref":
            r = self.new()
            self.ops.append(f"cfl {r} {norm(t[1])}" if strip_subs(t[1]) in self.rel else f"cfl {r}")
            return r
        if k in ("bin", "rel", "lbin"):
            a = self.emit(t[1])
            b = self.emit(t[2])
            r = self.new()
            self.ops.append(f"cu {r} {a} {b}")
            return r
        if k == "multi" or k == "call":
            regs = [self.emit(x) for x in (t[1] if k == "multi" else t[2])]
            r = self.new()
            self.ops.append(("cul " + str(r) + " " + " ".join(str(x) for x in reversed(regs))).rstrip())
            return r
        if k == "if":
            a = self.emit(t[2])
            b = self.emit(t[3])
            c = self.emit(t[1])
            r = self.new()
            self.ops.append(f"cul {r} {c} {a} {b}")
            return r
        raise Unsup(k)


def scalar_row_ops(lhs: str, rhs: str, relevant: set) -> list[str]:
    """SCALAR_EQUATION（NBAdjacency.mo:1602-1605）：union(occ(lhs), occ(rhs))。末尾 cdump 打出 to_list。"""
    ops: list[str] = []
    em = Emit(relevant, ops)
    a = em.emit(parse_expr(lhs))
    b = em.emit(parse_expr(rhs))
    r = em.new()
    ops.append(f"cu {r} {a} {b}")
    ops.append(f"cdump {r}")
    return ops


class Drv:
    def __init__(self, exe: str, tmp: str):
        self.exe = exe
        self.tmp = tmp

    def run(self, ops: list[str]) -> list[str]:
        with open(self.tmp, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(ops) + "\n")
        r = subprocess.run([self.exe, self.tmp], capture_output=True, timeout=OMC_TIMEOUT)
        if r.returncode != 0:
            raise RuntimeError(r.stderr.decode(errors="replace")[:500])
        return r.stdout.decode("utf-8", "replace").replace("\r\n", "\n").split("\n")


def list_of_dump(line: str) -> str:
    return re.search(r" L=(.*) A=", line).group(1)


# ---- when 拆分 ----

def when_bodies(lines: list[str], a: int, b: int) -> tuple[dict, list[list[int]]]:
    """(After Lowering) 里的 when。返回 ({k: [被赋变量…]}, [每个桶按打印序的 k…])。"""
    res: dict = {}
    printed: dict = {}
    bucket = ""
    i = a
    while i < b:
        h = re.match(r"^(\S.*) Equations \(\d+/\d+\)", lines[i])
        if h:
            bucket = h.group(1)
        m = re.match(r"^\((\d+)\)\s+\[WHEN\] \((\d+)\) \((\$RES_SIM_(\d+))\)", lines[i])
        if m:
            k = int(m.group(4))
            idx = m.group(1)
            j = i + 1
            vs: list[str] = []
            while j < b and lines[j].startswith("(" + idx + ")") and "[----]" in lines[j]:
                body = lines[j].split("[----]", 1)[1]
                m2 = re.match(r"\s+(\S.*?) := ", body)
                m3 = re.match(r"\s+reinit\(([^,]+),", body)
                if m2:
                    vs.append(m2.group(1))
                elif m3:
                    vs.append(m3.group(1))
                j += 1
            res[k] = vs
            printed.setdefault(bucket, []).append(k)
            i = j
            continue
        i += 1
    return res, list(printed.values())


def check_when(name: str, cwd: str, bodies: dict, printed: list, st: collections.Counter, fails: list, drv: Drv) -> None:
    """两种序，别读混：

    split 内部序（`WhenEquationBody.split` 的返回值，NBEquation.mo:3151-3257）：先 discretes，后 states。
      - discretes = keyList(discr_map)，插入序，与哈希无关。lowerWhenBranchBody 头插，语句表本身是源序的反向；elsewhen 分支接在后面，已有的键不动位置。
      - states = toList(state_set)，桶序。
      - 元组左端：一组键一起进 discr_map（toList(extractCrefs) 的序），只出一个 body。
      这个序在输出里的读法是 $RES_SIM_k 的 k 升序：lowerEquationsAndAlgorithms 把 bodies 整块接到表头（NBackendDAE.mo:878），createName 从表头编号（:834-836），
      所以模型最后一条方程是 when 时，bodies[k] 就是 $RES_SIM_k。这里的探针都满足这一点。

    打印序（dump 里 `Discrete Equations` / `Removed Equations` 的列举序）：是上面的序**再反一次**。BEquation.typeList（NBEquation.mo:4661）对每个桶头插，
      所以多赋值 when 打出来恰好是源序，reinit 打出来是 toList 的反向。这里按 k 递减校验（print 项）。S5a-1 的逐字节门比的是打印序，读 split 代码时不要直接拿来比。
    """
    src = open(os.path.join(cwd, "model.mo"), encoding="utf-8").read()
    body = re.search(r"\n  when (.*?)end when;", src, re.S).group(1)
    branches = [b for b in re.split(r"\n\s*elsewhen .*? then\n|^.*? then\n", body) if b.strip()]
    states: list[str] = []
    groups: list[list[str]] = []
    for b in branches:
        for l in reversed([x.strip() for x in b.split("\n") if x.strip()]):
            mr = re.match(r"reinit\((\S+?),", l)
            mt = re.match(r"\((.*)\) = ", l)
            ma = re.match(r"(\S+) = ", l)
            if mr:
                states.append(mr.group(1))
            elif mt:
                groups.append([x.strip() for x in mt.group(1).split(",")])
            elif ma:
                groups.append([ma.group(1)])

    # 元组一组：先取 toList 再逐个进 keyList。单个键的序与哈希无关，所以这里只用 Python 列表表达 keyList。
    ops: list[str] = []
    for g in groups:
        if len(g) > 1:
            ops.append("cn 40 -1")
            ops += [f"ca 40 {x}" for x in g]
            ops.append("cdump 40")
    tup = [l for l in drv.run(ops) if l.startswith("C40")] if ops else []
    ti = 0
    keys: list[str] = []
    for g in groups:
        ks = g
        if len(g) > 1:
            ks = list_of_dump(tup[ti]).split(",")
            ti += 1
        keys += [k for k in ks if k not in keys]
    # discr_marks：同组只出一个 body
    gmap = {x: g for g in groups for x in g}
    marks: set = set()
    disc_bodies: list[str] = []
    for k in keys:
        if k not in marks:
            marks.update(gmap[k])
            disc_bodies.append(k)
    ops = ["cn 41 -1"] + [f"ca 41 {x}" for x in dict.fromkeys(states)] + ["cdump 41"]
    tl = list_of_dump([l for l in drv.run(ops) if l.startswith("C41")][0])
    want = disc_bodies + (tl.split(",") if tl else [])

    got = [bodies[k] for k in sorted(bodies)]
    st["when-lists"] += 1
    st["when-items"] += len(want)
    ok = len(want) == len(got) and all(any(re.search(r"(^|[(, ])" + re.escape(a) + r"($|[), ])", v) for v in b) for a, b in zip(want, got))
    if not ok:
        st["when-fail"] += 1
        fails.append((name, "when", 0, want, got, ""))
    for group in printed:
        st["print-items"] += len(group)
        if any(group[i] <= group[i + 1] for i in range(len(group) - 1)):
            st["print-fail"] += 1
            fails.append((name, "print", 0, "k 严格递减", group, ""))


# ---- 一个模型 ----

def check_model(name: str, cwd: str, st: collections.Counter, fails: list, drv: Drv) -> None:
    txt = open(os.path.join(cwd, "out.txt"), "rb").read().decode("utf-8", "replace").replace("\r\n", "\n")
    lines = txt.split("\n")
    secs = split_sections(lines)
    st["models"] += 1
    part = [s for s in secs if s[0].endswith("(Partitioning)") and s[0].startswith("[ODE]")]
    evs = [s for s in secs if s[0] == "BackendDAE: (Events)"]
    low = [s for s in secs if s[0] == "BackendDAE: (After Lowering)"]
    if not part:        # 没有未知量时分区为空，打的是 `BackendDAE: (Partitioning)`（NBackendDAE.mo:150-169）
        st["no-partition"] += 1
        return
    parts = parse_partitions(lines, part[0][1], part[0][2])
    if not evs:
        return
    vd = parse_vardata(lines, evs[0][1], evs[0][2])
    causal = [s for s in secs if s[0].endswith("(Causalize)")]
    rows = parse_full(lines, part[0][2] - 1, causal[0][1] if causal else len(lines))
    useq = {n: i for i, n in enumerate(vd.get("Algebraic Variables", []) + vd.get("Derivative Variables", []))}
    eseq = {e["name"]: i for i, e in enumerate(vd.get("Continuous Equations", []))}

    ops: list[str] = []
    exp: list[tuple] = []      # 每条 cdump 对应的期望
    mono: list[tuple] = []
    for pi, p in enumerate(parts):
        if p["vars"] is None or p["eqs"] is None:
            continue
        unknowns = [norm(x) for x in p["vars"]]
        eqnames = [e["name"] for e in p["eqs"]]
        for lab, seq in (("U", unknowns), ("E", eqnames)):
            if seq and not any(x is None or "'" in x for x in seq):
                mono.append((lab, pi, seq))
        for lab, seq, order in (("order-U", unknowns, useq), ("order-E", eqnames, eseq)):
            if seq and all(x in order for x in seq):
                ops.append("cn 1 -1")
                ops += [f"ca 1 {x}" for x in sorted(seq, key=lambda n: order[n])]
                ops.append("cdump 1")
                exp.append((lab, pi, seq))
        if pi < len(rows):
            relevant = set(strip_subs(x) for x in unknowns)
            for e in p["eqs"]:
                t = e["text"]
                if e["tag"] != "SCAL" or e["name"] not in rows[pi] or " = " not in t:
                    st["row-skip"] += 1
                    continue
                lhs, rhs = t.split(" = ", 1)
                try:
                    row_ops = scalar_row_ops(lhs, rhs, relevant)
                except Unsup:
                    st["row-skip"] += 1
                    continue
                ops += row_ops
                exp.append(("row", pi, [norm(x) for x in rows[pi][e["name"]]], e["name"], t))
    if ops:
        got = [l for l in drv.run(ops) if l.startswith("C")]
        if len(got) != len(exp):
            raise RuntimeError(f"{name}: dump {len(got)} / 期望 {len(exp)}")
        for g, x in zip(got, exp):
            kind = x[0].split("-")[0]
            st[kind + "-lists"] += 1
            st[kind + "-items"] += len(x[2])
            want = ",".join(x[2])    # 键里有逗号（下标），不能切开比，整串比
            if list_of_dump(g) != want:
                st[kind + "-fail"] += 1
                fails.append((name, x[0], x[1], want, list_of_dump(g), x[3:] if len(x) > 3 else ""))
    if mono:
        out = [l for l in drv.run(["hcr " + n for _, _, seq in mono for n in seq]) if l.startswith("hcr ")]
        k = 0
        for lab, pi, seq in mono:
            b = set_buckets(len(seq))
            bk = [int(out[k + i].split()[1]) % b for i in range(len(seq))]
            k += len(seq)
            st["mono-lists"] += 1
            st["mono-items"] += len(seq)
            if any(bk[i] < bk[i + 1] for i in range(len(bk) - 1)):
                st["mono-fail"] += 1
                fails.append((name, "mono-" + lab, pi, seq, bk, ""))
    if low and name.startswith("HshW"):
        bodies, printed = when_bodies(lines, low[0][1], low[0][2])
        if bodies:
            check_when(name, cwd, bodies, printed, st, fails, drv)


def report(st: collections.Counter, fails: list) -> None:
    for k in sorted(st):
        print(k, st[k])
    print("fails", len(fails))
    groups: collections.Counter = collections.Counter()
    sample: dict = {}
    for x in fails:
        text = x[5][1] if len(x) > 5 and x[5] else ""
        key = (x[1], re.sub(r"[$\w.]+(\[[^\]]*\])?", "v", text))
        groups[key] += 1
        sample.setdefault(key, x)
    for key, n in groups.most_common(20):
        print(n, key, "例：", sample[key][0])
        print("   期望", str(sample[key][3])[:160])
        print("   预测", str(sample[key][4])[:160])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--drv", required=True, help="omc_hash_drv 可执行文件")
    ap.add_argument("--run-dir", required=True, help="放 omc 输出的目录，已有 out.txt 的模型不重跑")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--probes", help=".probes 包")
    src.add_argument("--list", help="清单文件：名字<TAB>model.mo 路径[<TAB>额外 omc 参数…]")
    ap.add_argument("--jobs", type=int, default=3, help="omc 并发数")
    a = ap.parse_args()
    mkhsh.check_omc()

    run_dir = os.path.abspath(a.run_dir)
    os.makedirs(run_dir, exist_ok=True)
    jobs: list[tuple] = []
    if a.probes:
        with open(a.probes, encoding="utf-8") as f:
            parts = re.split(r"^//// model: (\S+)\n", f.read(), flags=re.M)
        for i in range(1, len(parts), 2):
            jobs.append((parts[i], parts[i + 1], os.path.join(run_dir, parts[i]), []))
    else:
        with open(a.list, encoding="utf-8") as f:
            for l in f:
                l = l.rstrip("\n")
                if l:
                    c = l.split("\t")
                    with open(c[1], encoding="utf-8", errors="replace") as m:
                        jobs.append((c[0], m.read(), os.path.join(run_dir, c[0]), c[2:]))
    with ThreadPoolExecutor(a.jobs) as ex:
        list(ex.map(run_omc, jobs))

    drv = Drv(os.path.abspath(a.drv), os.path.join(run_dir, "tmp_ops.txt"))
    st: collections.Counter = collections.Counter()
    fails: list = []
    for j in jobs:
        try:
            check_model(j[0], j[2], st, fails, drv)
        except (RuntimeError, KeyError, IndexError, ValueError, AttributeError) as e:
            st["crash"] += 1
            print("CRASH", j[0], type(e).__name__, str(e)[:200])
    report(st, fails)
    with open(os.path.join(run_dir, "fails.json"), "w", encoding="utf-8") as f:
        json.dump(fails, f, ensure_ascii=False, indent=0)
    return 1 if fails or st["crash"] else 0


if __name__ == "__main__":
    sys.exit(main())
