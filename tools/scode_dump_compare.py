#!/usr/bin/env python3
"""S2 元素级验收：ymc 的 SCode dump 与 OMC `list(C, exportKind=SCode)` 逐行对比（方案 §5 S2 验收的加强版）。

OMC 侧：一次 omc 会话里 loadModel 后对每个类 print(list(C, exportKind=OpenModelica.Scripting.ExportKind.SCode))，
        类之间用标记行分隔（安装版 1.25.0 必须写全限定的 ExportKind）。
ymc 侧：ymc --modelica-path=... --load-model=P --version=V --dump-after=scode --class=C（每类一进程）。
比较：去行尾空白、折叠连续空行后逐行比；报告每类 PASS/DIFF（首个不同行）。

用法：
  python tools/scode_dump_compare.py --package Modelica --version 4.0.0 --classes Modelica.Blocks.Continuous.PID Modelica.Electrical.Analog.Basic.Resistor
  python tools/scode_dump_compare.py --package Modelica --version 4.0.0 --sample 40 --seed 1
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

MARK = "@@YMC_CLASS@@"


def norm(text):
    """行归一化：去行尾空白、折叠连续空行；多行字符串字面量的续行去掉前导空白。

    为什么：OMC 的模板引擎（Susan）把插入文本里的换行按当前缩进重排，多行字符串的续行会被
    加上模板层级决定的缩进（同一文件里类注释 +4、组件注解 −2、方程串 ±0，见 tasks/todo.md S2 记录），
    这改变了字符串内容本身，是 dump 的失真而非 SCode 差异，所以不比较续行的前导空白。
    """
    lines = text.replace("\r\n", "\n").split("\n")
    out = []
    in_str = False
    for l in lines:
        # if 表达式有 ≥2 个 elseif 时，OMC 模板在 elseif 子句之间插换行（AbsynDumpTpl.tpl dumpElseIfExp 的
        # separator="\n"，探针 probe-s2/emptymod.mos）——表达式内部的折行不是结构差异：并回上一行。
        # if 方程/语句的 elseif 行前一行以 ';' 或 'then' 结尾，不受影响。
        if (not in_str and l.lstrip().startswith("elseif ") and out
                and not out[-1].endswith((";", "then")) and out[-1] != ""):
            prev = out.pop()
            l = prev + " " + l.strip()
        if in_str:
            l = l.lstrip()
        i = 0
        while i < len(l):
            c = l[i]
            if in_str:
                if c == "\\":
                    i += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            i += 1
        l = l.rstrip()
        if l == "" and out and out[-1] == "":
            continue
        out.append(l)
    while out and out[-1] == "":
        out.pop()
    while out and out[0] == "":
        out.pop(0)
    return out


def omc_dumps(package, version, classes, timeout=600):
    ver = f', {{"{version}"}}' if version else ""
    script = [f"loadModel({package}{ver}); getErrorString();"]
    for c in classes:
        script.append(f'print("{MARK} {c}\\n");')
        script.append(f"print(list({c}, exportKind=OpenModelica.Scripting.ExportKind.SCode)); print(\"\\n\");")
    with tempfile.NamedTemporaryFile("w", suffix=".mos", delete=False, encoding="utf-8") as f:
        f.write("\n".join(script) + "\n")
        path = f.name
    try:
        p = subprocess.run(["omc", path], capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
    finally:
        os.unlink(path)
    result = {}
    cur = None
    buf = []
    for line in p.stdout.split("\n"):
        if line.startswith(MARK):
            if cur is not None:
                result[cur] = "\n".join(buf)
            cur = line[len(MARK):].strip()
            buf = []
        elif cur is not None:
            buf.append(line)
    if cur is not None:
        result[cur] = "\n".join(buf)
    return result


def ymc_dump(ymc, package, version, modelica_path, cls, timeout=300):
    cmd = [ymc, f"--modelica-path={modelica_path}", f"--load-model={package}"]
    if version:
        cmd.append(f"--version={version}")
    cmd += ["--dump-after=scode", f"--class={cls}"]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding="utf-8",
                       errors="replace")
    return p.returncode, p.stdout, p.stderr


def sample_classes(package, version, n, seed):
    from scode_compare import omc_class_names  # 复用 oracle 缓存
    _, rec = omc_class_names(package, version)
    random.seed(seed)
    return random.sample(rec, min(n, len(rec)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--package", required=True)
    ap.add_argument("--version", default=None)
    ap.add_argument("--modelica-path",
                    default=os.path.join(os.environ.get("APPDATA", ""), ".openmodelica", "libraries"))
    ap.add_argument("--classes", nargs="*", default=None)
    ap.add_argument("--sample", type=int, default=0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--context", type=int, default=2)
    ap.add_argument("--show", type=int, default=10, help="最多展示多少个 DIFF 的详情")
    ap.add_argument("--width", type=int, default=110, help="展示行截断宽度")
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

    classes = args.classes or []
    if args.sample:
        classes += sample_classes(args.package, args.version, args.sample, args.seed)
    if not classes:
        print("no classes", file=sys.stderr)
        return 2
    omc = omc_dumps(args.package, args.version, classes)
    passed, quirks = 0, []
    details = []
    for c in classes:
        o = norm(omc.get(c, ""))
        # OMC 交互 API 的怪癖（探针 2026-09-16，omc 1.25.0）：list(A.B) 在 A 同时含 `extends B;` 与嵌套类 B 时
        # 返回的是 extends 子句（按简单名匹配到了 EXTENDS 元素），不是类；不算 ymc 的差异，单独计数
        if o and o[0].startswith("extends ") and len(o) == 1:
            quirks.append(c)
            continue
        rc, out, err = ymc_dump(args.ymc, args.package, args.version, args.modelica_path, c)
        y = norm(out)
        if o == y and rc == 0:
            passed += 1
            continue
        first = None
        for i, (a, b) in enumerate(zip(o, y)):
            if a != b:
                first = i
                break
        if first is None:
            first = min(len(o), len(y))
        details.append((c, rc, o, y, first, err))
    n = len(classes) - len(quirks)
    print(f"== scode_dump_compare {args.package} {args.version or '(default)'}: {passed}/{n} 类逐行一致"
          f"{f'（另 {len(quirks)} 个 OMC list() 返回 extends 子句，不计）' if quirks else ''} ==")
    for c, rc, o, y, first, err in details[: args.show]:
        print(f"\n--- DIFF {c} (rc={rc}, omc {len(o)} 行 / ymc {len(y)} 行, 首个不同行 {first+1})")
        lo, hi = max(0, first - args.context), first + args.context + 1
        for i in range(lo, hi):
            a = o[i] if i < len(o) else "<EOF>"
            b = y[i] if i < len(y) else "<EOF>"
            flag = "  " if a == b else "!!"
            print(f"  {flag} omc | {a[:args.width]}")
            if a != b:
                print(f"  {flag} ymc | {b[:args.width]}")
        if err.strip():
            print("  ymc stderr:", err.strip().splitlines()[0][:120])
    if len(details) > args.show:
        print(f"\n（另有 {len(details) - args.show} 个 DIFF 未展示；类名：{', '.join(d[0] for d in details[args.show:args.show+15])}）")
    return 0 if passed == n else 1


if __name__ == "__main__":
    sys.exit(main())
