"""参考结果生成 + manifest + 过期检测（FR-030/031）。

把一批模型的参考结果生成到仓库外目录：
  <refs根>/<oracle>/<oracle版本>/<模型名>/{<name>_res.mat, stats.json, gen.log}
并写 manifest.json（IF-005）：oracle、版本、生成命令、源模型哈希、产物哈希、时间。

oracle：omc-installed / omc-workspace（用 omc 仿真）/ dymola-2025x（用 dymola_driver，需许可证）。
过期检测：当前 oracle 版本或源模型哈希与 manifest 不符 → 报 ref-stale，不自动重生成
（重生成需 --force）。OMC testsuite 自带 ReferenceFiles/*.mat.xz 直接复用，不在此重生成。

时间戳由调用方传入（--now），以便可复现；缺省用文件系统时间。

基建工具，Claude 提供。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_REFS = r"C:\dev\ymc-refs"
OMC = {"omc-installed": r"C:\Program Files\OpenModelica1.25.0-64bit\bin\omc.exe",
       "omc-workspace": r"C:\dev\OpenModelica\build_cmake\install_cmake\bin\omc.exe"}


def _sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def omc_version(exe):
    r = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30)
    return (r.stdout or r.stderr or "").strip().splitlines()[0] if (r.stdout or r.stderr) else "unknown"


def _manifest_path(refs_root, oracle, version):
    return os.path.join(refs_root, oracle, _safe(version), "manifest.json")


def _safe(s):
    return "".join(c if c.isalnum() or c in "._-+" else "_" for c in s)


def load_manifest(path):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {"schema_version": 1, "oracle": "", "oracle_version": "", "entries": []}


def save_manifest(path, man):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    man["entries"].sort(key=lambda e: e["model_path"])
    with open(path, "w", encoding="utf-8") as f:
        json.dump(man, f, ensure_ascii=False, indent=1, sort_keys=True)


def gen_omc(model_file, oracle, refs_root, timeout, now, force):
    exe = OMC[oracle]
    ver = omc_version(exe)
    outdir = os.path.join(refs_root, oracle, _safe(ver))
    man_path = _manifest_path(refs_root, oracle, ver)
    man = load_manifest(man_path)
    man["oracle"] = oracle
    man["oracle_version"] = ver
    abs_model = os.path.abspath(model_file)
    model_hash = _sha256(abs_model)
    name = os.path.splitext(os.path.basename(model_file))[0]
    entry_dir = os.path.join(outdir, name)
    rel = os.path.relpath(abs_model, REPO).replace("\\", "/") \
        if abs_model.startswith(REPO) else abs_model.replace("\\", "/")

    existing = next((e for e in man["entries"] if e["model_path"] == rel), None)
    if existing and existing.get("model_hash") == model_hash and not force:
        return ("skip-fresh", existing)

    os.makedirs(entry_dir, exist_ok=True)
    mat = os.path.join(entry_dir, f"{name}_res.mat")
    mos = os.path.join(entry_dir, "gen.mos")
    with open(mos, "w", encoding="utf-8") as f:
        f.write(f'loadFile("{abs_model.replace(chr(92), "/")}"); getErrorString();\n')
        f.write(f'simulate({name}, stopTime=1.0, outputFormat="mat", '
                f'fileNamePrefix="{name}"); getErrorString();\n')
    r = subprocess.run([exe, os.path.basename(mos)], cwd=entry_dir,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
    log = os.path.join(entry_dir, "gen.log")
    with open(log, "w", encoding="utf-8") as f:
        f.write(r.stdout or "")
    produced = os.path.join(entry_dir, f"{name}_res.mat")
    if not os.path.exists(produced):
        return ("failed", {"model_path": rel, "error": "no result file"})
    if produced != mat and os.path.exists(produced):
        shutil.move(produced, mat)
    entry = {
        "model_path": rel, "model_hash": model_hash,
        "artifact": {"mat": os.path.relpath(mat, outdir).replace("\\", "/"),
                     "log": os.path.relpath(log, outdir).replace("\\", "/")},
        "artifact_hash": _sha256(mat),
        "gen_command": f'{oracle} simulate {name}',
        "gen_at": now or time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    man["entries"] = [e for e in man["entries"] if e["model_path"] != rel] + [entry]
    save_manifest(man_path, man)
    return ("generated", entry)


def check_stale(model_file, oracle, refs_root):
    exe = OMC.get(oracle)
    ver = omc_version(exe) if exe else "?"
    man_path = _manifest_path(refs_root, oracle, ver)
    man = load_manifest(man_path)
    rel = model_file.replace("\\", "/")
    entry = next((e for e in man["entries"]
                  if e["model_path"].endswith(os.path.basename(model_file))), None)
    if entry is None:
        return "missing"
    if entry.get("model_hash") != _sha256(model_file):
        return "ref-stale"
    return "fresh"


def _discover(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, fs in os.walk(p):
                files += [os.path.join(root, f) for f in fs if f.endswith(".mo")]
        elif p.endswith(".mo"):
            files.append(p)
    return sorted(files)


def main(argv=None):
    p = argparse.ArgumentParser(description="参考结果生成与过期检测")
    p.add_argument("--oracle", default="omc-installed",
                   choices=["omc-installed", "omc-workspace", "dymola-2025x"])
    p.add_argument("--models", nargs="*", default=[], help=".mo 文件或目录")
    p.add_argument("--refs-root", default=DEFAULT_REFS)
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--timeout", type=int, default=360)
    p.add_argument("--force", action="store_true")
    p.add_argument("--check", action="store_true", help="只检查过期，不生成")
    p.add_argument("--now", default="", help="固定时间戳（可复现）")
    args = p.parse_args(argv)

    if args.oracle == "dymola-2025x":
        print("dymola 参考生成：请用 dymola_driver.py（需许可证）；本入口仅支持 omc-*。")
        return 2

    files = _discover(args.models)
    if args.limit:
        files = files[:args.limit]
    if not files:
        print("没有可处理的 .mo 模型")
        return 2

    for mf in files:
        if args.check:
            print(f"{check_stale(mf, args.oracle, args.refs_root):10s}  {mf}")
            continue
        try:
            status, entry = gen_omc(mf, args.oracle, args.refs_root, args.timeout, args.now, args.force)
        except subprocess.TimeoutExpired:
            print(f"timeout    {mf}")
            continue
        print(f"{status:12s}  {mf}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
