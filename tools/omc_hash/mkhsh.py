#!/usr/bin/env python3
"""准备 L0 / L1 的 oracle 目录：把 OMC v1.25.0 的哈希容器原文放进工作目录，由安装版 omc 现场编译。

为什么要这样：容器内部从后端输出看不见，而 omc 能在脚本里编译 MetaModelica。把 tag 上的 UnorderedSet.mo / UnorderedMap.mo 原文编译后跑在
它自己的 Windows 运行时上，哈希就是真的 stringHashDjb2（32 位 long），桶数也能直接读。这一层跑的是相同源码和运行时，不是 omc 二进制里那份
编译结果；二进制本身由 L2（chk.py）覆盖。

生成的文件：
  UnorderedSet.mo UnorderedMap.mo Mutable.mo   tag 原文，逐字节
  Vector.mo                                    tag 原文，只给 `function new<T>` 加 impure：omc 脚本的前端会在编译期常量求值无参的 Vector.new<K>()，
                                               结果转 Value 时断言失败（Dynload.cpp:409）。函数体不变
  List.mo Util.mo                              只含用到的函数，函数体逐字节抄 tag
  Array.mo Error.mo IOStream.mo                桩，只走失败路径和 toJSON
  Hsh.mo                                       解释操作脚本的小函数（操作清单见 gen_ops.py）
  run.mos                                      Hsh.run(readFile("ops.txt"), 13, 1)

OMC 源码固定读 tag v1.25.0（git show），不看工作区 HEAD。

用法：
  python -I tools/omc_hash/mkhsh.py --work <工作目录> [--omc-src C:/dev/OpenModelica]
一般不用单独跑，diff.py 会调用。
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess

TAG = "v1.25.0"
DEFAULT_OMC_SRC = "C:/dev/OpenModelica"
UTIL = "OMCompiler/Compiler/Util/"
OMC_VERSION = "v1.25.0"

MOS = """setCommandLineOptions("-g=MetaModelica -d=gen,noevalfunc"); getErrorString();
loadFiles({"Mutable.mo","Vector.mo","Array.mo","List.mo","Util.mo","Error.mo","IOStream.mo","UnorderedSet.mo","UnorderedMap.mo","Hsh.mo"}); getErrorString();
Hsh.run(readFile("ops.txt"), 13, 1); getErrorString();
"""

ERROR_STUB = """encapsulated package Error
function addInternalError
  input String message;
  input SourceInfo info;
algorithm
  print(message);
end addInternalError;
end Error;
"""

IOSTREAM_STUB = """encapsulated package IOStream
uniontype IOStreamType record LIST end LIST; end IOStreamType;
uniontype IOStream record IOSTREAM list<String> data; end IOSTREAM; end IOStream;
function create
  input String name;
  input IOStreamType t;
  output IOStream s = IOSTREAM({});
end create;
function append
  input output IOStream s;
  input String x;
algorithm
  s := IOSTREAM(x :: s.data);
end append;
function string
  input IOStream s;
  output String x;
algorithm
  x := stringAppendList(listReverse(s.data));
end string;
end IOStream;
"""

HEAD = """encapsulated package Hsh
import UnorderedSet;
import UnorderedMap;
import Util;
protected
import MetaModelica.Dangerous.*;
public

function idh
  input Integer i;
  output Integer o = i;
end idh;

function splitChar
  input String s;
  input String ch;
  output list<String> out = {};
protected
  list<String> cs = stringListStringChar(s);
  list<String> cur = {};
algorithm
  for c in cs loop
    if stringEq(c, ch) then
      out := stringAppendList(listReverse(cur)) :: out;
      cur := {};
    else
      cur := c :: cur;
    end if;
  end for;
  if not listEmpty(cur) then
    out := stringAppendList(listReverse(cur)) :: out;
  end if;
  out := listReverse(out);
end splitChar;

function intStrs
  input list<Integer> l;
  output list<String> o = list(intString(i) for i in l);
end intStrs;

function strStrs
  input list<String> l;
  output list<String> o = l;
end strStrs;

function appS0
  input output String k;
algorithm
  k := k + "z";
end appS0;
function appS1
  input output String k;
algorithm
  if stringLength(k) > 2 then
    k := substring(k, 1, 2);
  end if;
end appS1;
function evenS
  input String k;
  output Boolean b = intMod(stringLength(k), 2) == 0;
end evenS;
function appI0
  input output Integer k;
algorithm
  k := k * 2;
end appI0;
function appI1
  input output Integer k;
algorithm
  k := intMod(k, 7);
end appI1;
function evenI
  input Integer k;
  output Boolean b = intMod(k, 2) == 0;
end evenI;
function upd
  input Option<Integer> old;
  input Integer d;
  output Integer v;
algorithm
  v := match old
    local Integer x;
    case SOME(x) then x + d;
    else d;
  end match;
end upd;

// stringInt 只收 32 位范围（MODELICA_INT_MAX = LONG_MAX），宽入参拆成两段拼。
function opH
  input String op;
  input list<String> toks;
  output Boolean handled = true;
protected
  String a, b, c;
algorithm
  if op == "hd" then
    print("hd " + intString(stringHashDjb2(listHead(toks))) + "\\n");
  elseif op == "hc" then
    {a, b, c} := toks;
    print("hc " + intString(stringHashDjb2Continue(a, stringInt(b) * 65536 * 65536 + stringInt(c))) + "\\n");
  elseif op == "hm" then
    {a, b, c} := toks;
    print("hm " + intString(intMod(stringInt(a) * 65536 + stringInt(b), stringInt(c))) + "\\n");
  elseif op == "hp" then
    print("hp " + intString(Util.nextPrime(stringInt(listHead(toks)))) + "\\n");
  else
    handled := false;
  end if;
end opH;
"""

SET = """
function dump{P}
  input Integer r;
  input UnorderedSet<{T}> s;
algorithm
  print("{P}" + intString(r) + " n=" + intString(UnorderedSet.size(s)) + " b=" + intString(UnorderedSet.bucketCount(s))
    + " L=" + stringDelimitList({STRS}(UnorderedSet.toList(s)), ",")
    + " A=" + stringDelimitList({STRS}(arrayList(UnorderedSet.toArray(s))), ",") + "\\n");
end dump{P};

function op{P}
  input String op;
  input list<String> toks;
  input array<UnorderedSet<{T}>> R;
  input Integer dflt;
  output Boolean handled = true;
protected
  Integer r, a, b;
  list<Integer> rest;
  {T} k;
  list<{T}> ks;
  list<UnorderedSet<{T}>> sets;
  UnorderedSet<{T}> s1, s2;
  Integer md;
algorithm
  if op == "{p}n" then
    r :: rest := list(stringInt(t) for t in toks);
    a :: _ := rest;
    if a < 0 then
      arrayUpdate(R, r + 1, UnorderedSet.new({HASH}, {EQ}, dflt));
    else
      arrayUpdate(R, r + 1, UnorderedSet.new({HASH}, {EQ}, a));
    end if;
  elseif op == "{p}fl" then
    r := stringInt(listHead(toks));
    ks := list({PARSE}(t) for t in listRest(toks));
    arrayUpdate(R, r + 1, UnorderedSet.fromList(ks, {HASH}, {EQ}));
  elseif op == "{p}a" then
    UnorderedSet.add({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1));
  elseif op == "{p}an" then
    UnorderedSet.addNew({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1));
  elseif op == "{p}au" then
    try
      UnorderedSet.addUnique({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1));
      print("au ok\\n");
    else
      print("au fail\\n");
    end try;
  elseif op == "{p}r" then
    print("r " + boolString(UnorderedSet.remove({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1))) + "\\n");
  elseif op == "{p}c" then
    print("c " + boolString(UnorderedSet.contains({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1))) + "\\n");
  elseif op == "{p}u" then
    {{r, a, b}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedSet.union(arrayGet(R, a + 1), arrayGet(R, b + 1)));
  elseif op == "{p}ul" then
    r := stringInt(listHead(toks));
    sets := list(arrayGet(R, stringInt(t) + 1) for t in listRest(toks));
    arrayUpdate(R, r + 1, UnorderedSet.union_list(sets, {HASH}, {EQ}));
  elseif op == "{p}d" then
    {{r, a, b}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedSet.difference(arrayGet(R, a + 1), arrayGet(R, b + 1)));
  elseif op == "{p}sd" then
    {{r, a, b}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedSet.sym_difference(arrayGet(R, a + 1), arrayGet(R, b + 1)));
  elseif op == "{p}fof" then
    {{r, a}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedSet.filterOnFalse(arrayGet(R, a + 1), {EVEN}));
  elseif op == "{p}sot" then
    {{r, b, a}} := list(stringInt(t) for t in toks);
    (s1, s2) := UnorderedSet.splitOnTrue(arrayGet(R, a + 1), {EVEN});
    arrayUpdate(R, r + 1, s1);
    arrayUpdate(R, b + 1, s2);
  elseif op == "{p}ap" then
    {{r, md}} := list(stringInt(t) for t in toks);
    if md == 0 then
      UnorderedSet.apply(arrayGet(R, r + 1), {APP0});
    else
      UnorderedSet.apply(arrayGet(R, r + 1), {APP1});
    end if;
  elseif op == "{p}uq" then
    ks := list({PARSE}(t) for t in toks);
    print("uq " + stringDelimitList({STRS}(UnorderedSet.unique_list(ks, {HASH}, {EQ})), ",") + "\\n");
  elseif op == "{p}dump" then
    r := stringInt(listHead(toks));
    dump{P}(r, arrayGet(R, r + 1));
  else
    handled := false;
  end if;
end op{P};
"""

MAP = """
function dump{P}
  input Integer r;
  input UnorderedMap<{T}, Integer> m;
algorithm
  print("{P}" + intString(r) + " n=" + intString(UnorderedMap.size(m)) + " b=" + intString(UnorderedMap.bucketCount(m))
    + " K=" + stringDelimitList({STRS}(UnorderedMap.keyList(m)), ",")
    + " V=" + stringDelimitList(intStrs(UnorderedMap.valueList(m)), ",") + "\\n");
end dump{P};

function pairs{P}
  input list<String> toks;
  output list<{T}> ks = {{}};
  output list<Integer> vs = {{}};
protected
  list<String> t = toks;
  String a, b;
algorithm
  while not listEmpty(t) loop
    a :: b :: t := t;
    ks := {PARSE}(a) :: ks;
    vs := stringInt(b) :: vs;
  end while;
  ks := listReverse(ks);
  vs := listReverse(vs);
end pairs{P};

function op{P}
  input String op;
  input list<String> toks;
  input array<UnorderedMap<{T}, Integer>> R;
  input Integer dflt;
  output Boolean handled = true;
protected
  Integer r, a, b, v;
  String ks0, vs0;
  list<{T}> ks;
  list<Integer> vs;
  list<Integer> rest;
algorithm
  if op == "{p}n" then
    r :: rest := list(stringInt(t) for t in toks);
    a :: _ := rest;
    if a < 0 then
      arrayUpdate(R, r + 1, UnorderedMap.new<Integer>({HASH}, {EQ}, dflt));
    else
      arrayUpdate(R, r + 1, UnorderedMap.new<Integer>({HASH}, {EQ}, a));
    end if;
  elseif op == "{p}fl" then
    r := stringInt(listHead(toks));
    (ks, vs) := pairs{P}(listRest(toks));
    arrayUpdate(R, r + 1, UnorderedMap.fromLists(ks, vs, {HASH}, {EQ}));
  elseif op == "{p}a" then
    {{ks0, vs0}} := listRest(toks);
    UnorderedMap.add({PARSE}(ks0), stringInt(vs0), arrayGet(R, stringInt(listHead(toks)) + 1));
  elseif op == "{p}an" then
    {{ks0, vs0}} := listRest(toks);
    UnorderedMap.addNew({PARSE}(ks0), stringInt(vs0), arrayGet(R, stringInt(listHead(toks)) + 1));
  elseif op == "{p}au" then
    {{ks0, vs0}} := listRest(toks);
    try
      UnorderedMap.addUnique({PARSE}(ks0), stringInt(vs0), arrayGet(R, stringInt(listHead(toks)) + 1));
      print("au ok\\n");
    else
      print("au fail\\n");
    end try;
  elseif op == "{p}up" then
    {{ks0, vs0}} := listRest(toks);
    print("up " + intString(UnorderedMap.addUpdate({PARSE}(ks0), function upd(d = stringInt(vs0)), arrayGet(R, stringInt(listHead(toks)) + 1))) + "\\n");
  elseif op == "{p}r" then
    print("r " + boolString(UnorderedMap.remove({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1))) + "\\n");
  elseif op == "{p}g" then
    print("g " + intString(UnorderedMap.getOrDefault({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1), -999)) + "\\n");
  elseif op == "{p}c" then
    print("c " + boolString(UnorderedMap.contains({PARSE}(listHead(listRest(toks))), arrayGet(R, stringInt(listHead(toks)) + 1))) + "\\n");
  elseif op == "{p}m" then
    {{r, a, b}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedMap.merge(arrayGet(R, a + 1), arrayGet(R, b + 1), sourceInfo()));
  elseif op == "{p}sub" then
    r :: a :: _ := list(stringInt(t) for t in {{listHead(toks), listHead(listRest(toks))}});
    ks := list({PARSE}(t) for t in listRest(listRest(toks)));
    arrayUpdate(R, r + 1, UnorderedMap.subMap(arrayGet(R, a + 1), ks));
  elseif op == "{p}cp" then
    {{r, a}} := list(stringInt(t) for t in toks);
    arrayUpdate(R, r + 1, UnorderedMap.copy(arrayGet(R, a + 1)));
  elseif op == "{p}dump" then
    r := stringInt(listHead(toks));
    dump{P}(r, arrayGet(R, r + 1));
  else
    handled := false;
  end if;
end op{P};
"""

# 桶数走入参：写成字面量时前端会在编译期常量求值 new(...)，道理同 Vector.new。
TAIL = """
function run
  input String script;
  input Integer sdflt;
  input Integer mdflt;
  output Integer rc = 0;
protected
  array<UnorderedSet<String>> ss;
  array<UnorderedSet<Integer>> is;
  array<UnorderedMap<String, Integer>> ms;
  array<UnorderedMap<Integer, Integer>> ns;
  list<String> toks;
  String op;
algorithm
  ss := arrayCreate(64, UnorderedSet.new(stringHashDjb2, stringEqual, sdflt));
  is := arrayCreate(64, UnorderedSet.new(idh, intEq, sdflt));
  ms := arrayCreate(64, UnorderedMap.new<Integer>(stringHashDjb2, stringEqual, mdflt));
  ns := arrayCreate(64, UnorderedMap.new<Integer>(idh, intEq, mdflt));
  for line in splitChar(script, "\\n") loop
    toks := splitChar(line, " ");
    if not listEmpty(toks) then
      op :: toks := toks;
      try
        if op == "#" then
          print(line + "\\n");
        elseif opH(op, toks) then
        elseif opS(op, toks, ss, sdflt) then
        elseif opI(op, toks, is, sdflt) then
        elseif opM(op, toks, ms, mdflt) then
        elseif opN(op, toks, ns, mdflt) then
        else
          print("UNKNOWN " + op + "\\n");
        end if;
      else
        print("FAILED " + line + "\\n");
      end try;
    end if;
  end for;
end run;

end Hsh;
"""


def check_omc() -> None:
    """只认 PATH 上的安装版 1.25.0：工作区那个 omc.exe 不是 oracle。"""
    r = subprocess.run(["omc", "--version"], capture_output=True, text=True, timeout=120)
    if OMC_VERSION not in r.stdout:
        raise SystemExit(f"PATH 上的 omc 不是 {OMC_VERSION}：{r.stdout.strip()!r}")


def git_show(omc_src: str, path: str) -> str:
    r = subprocess.run(["git", "-C", omc_src, "show", f"{TAG}:{path}"], capture_output=True)
    if r.returncode != 0:
        raise SystemExit(f"读不到 {TAG}:{path}（{omc_src}）：{r.stderr.decode(errors='replace')}")
    return r.stdout.decode("latin-1")


def grab(src: str, name: str) -> str:
    """抠出顶层函数 name 的原文。"""
    m = re.search(r"^(public |protected )?function " + name + r"\b", src, re.M)
    if m is None:
        raise SystemExit(f"找不到函数 {name}")
    e = re.search(r"^end " + name + r";", src[m.start():], re.M)
    return src[m.start():m.start() + e.end()]


def hsh_mo() -> str:
    out = HEAD
    # 字符串键不用解析：{PARSE}(t) 变成 (t)
    out += SET.format(P="S", p="s", T="String", HASH="stringHashDjb2", EQ="stringEqual", PARSE="", STRS="strStrs",
                      EVEN="evenS", APP0="appS0", APP1="appS1")
    out += SET.format(P="I", p="i", T="Integer", HASH="idh", EQ="intEq", PARSE="stringInt", STRS="intStrs",
                      EVEN="evenI", APP0="appI0", APP1="appI1")
    out += MAP.format(P="M", p="m", T="String", HASH="stringHashDjb2", EQ="stringEqual", PARSE="", STRS="strStrs")
    out += MAP.format(P="N", p="n", T="Integer", HASH="idh", EQ="intEq", PARSE="stringInt", STRS="intStrs")
    return out + TAIL


def prepare(work: str, omc_src: str) -> None:
    os.makedirs(work, exist_ok=True)

    def write(name: str, text: str, encoding: str = "latin-1") -> None:
        with open(os.path.join(work, name), "w", encoding=encoding, newline="") as f:
            f.write(text)

    for name in ("UnorderedSet.mo", "UnorderedMap.mo", "Mutable.mo"):
        write(name, git_show(omc_src, UTIL + name))
    vector = git_show(omc_src, UTIL + "Vector.mo")
    if vector.count("  function new<T>\n") != 1:
        raise SystemExit("Vector.mo 里 `function new<T>` 不是恰好一处，打不上 impure")
    write("Vector.mo", vector.replace("  function new<T>\n", "  impure function new<T>\n"))

    lst = git_show(omc_src, UTIL + "List.mo")
    body = "".join(grab(lst, n) + "\n\n" for n in ("deleteMemberOnTrue", "append_reverse", "hasSeveralElements", "all", "zip"))
    write("List.mo", "encapsulated package List\nimport MetaModelica.Dangerous.{listReverseInPlace};\n" + body + "end List;\n")
    util = git_show(omc_src, UTIL + "Util.mo")
    body = "".join(grab(util, n) + "\n\n" for n in ("nextPrime", "nextPrime2", "nextPrime_isPrime"))
    write("Util.mo", "encapsulated package Util\n" + body + "end Util;\n")

    write("Array.mo", "encapsulated package Array\nend Array;\n")
    write("Error.mo", ERROR_STUB)
    write("IOStream.mo", IOSTREAM_STUB)
    write("Hsh.mo", hsh_mo(), "utf-8")
    write("run.mos", MOS, "utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", required=True, help="工作目录（omc 会在这里生成一堆 C 文件和 DLL）")
    ap.add_argument("--omc-src", default=DEFAULT_OMC_SRC, help="OpenModelica 源码仓库，固定读 tag " + TAG)
    a = ap.parse_args()
    prepare(a.work, a.omc_src)
    print(f"oracle 目录就绪：{a.work}")


if __name__ == "__main__":
    main()
