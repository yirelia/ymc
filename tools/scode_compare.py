#!/usr/bin/env python3
"""S2 验收：ymc 加载一个库后枚举出的类名集合/顺序，与 OMC `getClassNames` 对比（方案 §5 S2 验收）。

OMC 侧（oracle）：
  loadModel(P, {"V"}); getClassNames(P, recursive=true, qualified=true, showProtected=true);
  bare 语句回显格式为 `{A.B, A.C, ...}`。结果按 (omc 版本, P, V) 缓存到 scratchpad。
ymc 侧：
  ymc --modelica-path=<...> --load-model=P --version=V --dump-class-names
  约定输出：每行一个全限定类名，按声明顺序递归（先包自身，再依次子元素）。

判定：
  集合相等 → PASS；否则列出 ymc 缺的 / 多的；顺序不同只作 WARN（首个不同位置）。
用法：
  python tools/scode_compare.py --package Modelica --version 4.0.0
  python tools/scode_compare.py --package Complex --version 4.0.0 --no-cache
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

SCRATCH = os.environ.get(
    "YMC_SCRATCH",
    os.path.join(os.environ.get("TEMP", tempfile.gettempdir()), "ymc-scode-compare"))


def omc_version():
    p = subprocess.run(["omc", "--version"], capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    return p.stdout.strip().split()[-1] if p.stdout.strip() else "unknown"


def omc_class_names(package, version, use_cache=True, timeout=300):
    """返回 (top_level_list, recursive_list)。"""
    os.makedirs(SCRATCH, exist_ok=True)
    key = hashlib.md5(f"{omc_version()}|{package}|{version}".encode()).hexdigest()[:12]
    cache = os.path.join(SCRATCH, f"omc-names-{package}-{version or 'default'}-{key}.txt")
    if use_cache and os.path.exists(cache):
        with open(cache, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
        sep = lines.index("---")
        return lines[:sep], lines[sep + 1:]
    ver = f', {{"{version}"}}' if version else ""
    script = (f"loadModel({package}{ver}); getErrorString();\n"
              f"getClassNames();\n"
              f"getClassNames({package}, recursive=true, qualified=true, showProtected=true);\n")
    with tempfile.NamedTemporaryFile("w", suffix=".mos", delete=False, encoding="utf-8") as f:
        f.write(script)
        path = f.name
    try:
        p = subprocess.run(["omc", path], capture_output=True, text=True, timeout=timeout,
                           encoding="utf-8", errors="replace")
    finally:
        os.unlink(path)
    lists = re.findall(r"^\{(.*)\}\s*$", p.stdout, re.MULTILINE)
    if len(lists) < 2:
        print("omc 输出无法解析：\n" + p.stdout[:800] + p.stderr[:400], file=sys.stderr)
        sys.exit(2)
    top = [s.strip() for s in lists[0].split(",") if s.strip()]
    rec = [s.strip() for s in lists[1].split(",") if s.strip()]
    with open(cache, "w", encoding="utf-8") as f:
        f.write("\n".join(top) + "\n---\n" + "\n".join(rec) + "\n")
    return top, rec


def ymc_class_names(ymc, package, version, modelica_path, timeout=300):
    cmd = [ymc, f"--modelica-path={modelica_path}", f"--load-model={package}"]
    if version:
        cmd.append(f"--version={version}")
    cmd.append("--dump-class-names")
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, encoding="utf-8",
                       errors="replace")
    names, top = [], []
    for l in p.stdout.splitlines():
        l = l.strip()
        if l.startswith("# top:"):
            top = l[6:].split()
        elif l and not l.startswith("#"):
            names.append(l)
    return p.returncode, names, p.stderr, top


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ymc", default=os.path.join("build", "gcc-debug", "ymc.exe"))
    ap.add_argument("--package", required=True)
    ap.add_argument("--version", default=None)
    ap.add_argument("--modelica-path",
                    default=os.path.join(os.environ.get("APPDATA", ""), ".openmodelica", "libraries"))
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--show", type=int, default=30)
    args = ap.parse_args()
    args.ymc = os.path.abspath(args.ymc)

    top_o, rec_o = omc_class_names(args.package, args.version, use_cache=not args.no_cache)
    rc, rec_y, err, top_y = ymc_class_names(args.ymc, args.package, args.version, args.modelica_path)
    print(f"== scode_compare {args.package} {args.version or '(default)'} ==")
    print(f"omc  : top-level {len(top_o)} 个 {top_o}；递归 {len(rec_o)} 个类（含 root 与 protected）")
    print(f"ymc  : rc={rc}，top-level {len(top_y)} 个 {top_y}；递归 {len(rec_y)} 个类")
    if set(top_o) != set(top_y):
        print(f"顶层装载集合不同：omc 独有 {sorted(set(top_o)-set(top_y))}，ymc 独有 {sorted(set(top_y)-set(top_o))}")
    if err.strip():
        print("ymc stderr (前 6 行):")
        for l in err.strip().splitlines()[:6]:
            print("   ", l[:160])

    so, sy = set(rec_o), set(rec_y)
    missing = [n for n in rec_o if n not in sy]
    extra = [n for n in rec_y if n not in so]
    print(f"集合：ymc 缺 {len(missing)}，多 {len(extra)}")
    for n in missing[: args.show]:
        print(f"   缺 {n}")
    for n in extra[: args.show]:
        print(f"   多 {n}")
    order_ok = True
    if not missing and not extra:
        for i, (a, b) in enumerate(zip(rec_o, rec_y)):
            if a != b:
                order_ok = False
                print(f"顺序：第 {i+1} 个不同：omc={a}  ymc={b}（WARN）")
                break
    verdict = "PASS" if (rc == 0 and not missing and not extra and set(top_o) == set(top_y)) else "FAIL"
    print(f"结论：{verdict}" + ("" if order_ok else "（顺序有差异）"))
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
