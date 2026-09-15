"""Dymola 无窗口驱动 + 翻译日志统计解析（FR-020/021）。

用 Dymola 2025x Refresh 1 的 Python 接口（wheel dymola-2025.1）无窗口驱动 Dymola：
  DymolaInterface(showwindow=False) → SetDymolaCompiler("vs",...) + verifyCompiler()
  → cd(workdir) → openModel(path, changeDirectory=False) → translateModel/simulateExtendedModel
  → 每步 getLastError() 判 numErrors>0 为失败 → close()。

翻译统计从 getLastError() 的 message 文本正则解析（R3§3/§4；Python 接口不暴露 dsmodel.mof，
见 TBD-07）。

优雅降级（NFR-007）：若 dymola 模块不可导入（wheel 未安装）或启动失败，返回
dymola-unavailable，不抛断。安装：py -3.11 -m pip install "<Dymola>\...\python_interface\dymola-2025.1-py3-none-any.whl"

基建工具，Claude 提供。stats 解析可离线单测；实际驱动需本机 Dymola + 许可证。
"""
from __future__ import annotations

import argparse
import json
import os
import re

WHEEL_HINT = (r'C:\Program Files\Dymola 2025x Refresh 1\Modelica\Library'
              r'\python_interface\dymola-2025.1-py3-none-any.whl')


# ---------------------------------------------------------------- 统计解析
def parse_translation_stats(message):
    """从 getLastError() 的 message 文本解析翻译统计（R3§3）。缺失字段返回 None/空。"""
    stats = {}
    m = re.search(r"has\s+(\d+)\s+scalar unknowns and\s+(\d+)\s+scalar equations", message)
    if m:
        stats["scalar_unknowns"] = int(m.group(1))
        stats["scalar_equations"] = int(m.group(2))
    m = re.search(r"Number of components:\s*(\d+)", message)
    if m:
        stats["components"] = int(m.group(1))
    m = re.search(r"Continuous time states:\s*(\d+)", message)
    if m:
        stats["continuous_states"] = int(m.group(1))

    def _sizes(label):
        mm = re.search(label + r":\s*\{([^}]*)\}", message)
        if not mm:
            return None
        body = mm.group(1).strip()
        if not body:
            return []
        return [int(x) for x in re.findall(r"\d+", body)]

    lin = _sizes("Sizes of linear systems of equations")
    nlin = _sizes("Sizes of nonlinear systems of equations")
    if lin is not None:
        stats["linear_sizes"] = lin
    if nlin is not None:
        stats["nonlinear_sizes"] = nlin
    return stats


# ---------------------------------------------------------------- 驱动
class DymolaUnavailable(Exception):
    pass


class Driver:
    def __init__(self, dymolapath="", msvc_dir=None):
        try:
            from dymola.dymola_interface import DymolaInterface  # noqa
        except Exception as e:
            raise DymolaUnavailable(
                f"无法导入 dymola 模块（{e}）。请先安装 wheel：\n"
                f'  py -3.11 -m pip install "{WHEEL_HINT}"')
        from dymola.dymola_interface import DymolaInterface
        try:
            self.d = DymolaInterface(dymolapath=dymolapath, showwindow=False)
        except Exception as e:
            raise DymolaUnavailable(f"Dymola 启动失败（许可证/端口/路径）：{e}")
        self.version = self.d.DymolaVersion()
        if msvc_dir:
            self.d.SetDymolaCompiler("vs", [f"MSVCDir={msvc_dir}"])
        # verifyCompiler 失败不致命，但记录
        try:
            self.compiler_ok = bool(self.d.verifyCompiler())
        except Exception:
            self.compiler_ok = False

    def last_error(self):
        msg, nerr, nwarn, ninfo = self.d.getLastError()
        return msg, nerr, nwarn, ninfo

    def translate(self, model, workdir=None):
        if workdir:
            self.d.cd(workdir)
        self.d.translateModel(model)
        msg, nerr, _, _ = self.last_error()
        return {"ok": nerr == 0, "log": msg, "stats": parse_translation_stats(msg)}

    def simulate(self, model, stop=1.0, final_names=None, result="dymres", workdir=None):
        if workdir:
            self.d.cd(workdir)
        fn = final_names or []
        ok, vals = self.d.simulateExtendedModel(
            problem=model, stopTime=stop, resultFile=result, finalNames=fn)
        msg, nerr, _, _ = self.last_error()
        return {"ok": bool(ok) and nerr == 0, "final": list(vals) if vals else [],
                "log": msg, "result_file": result + ".mat"}

    def close(self):
        try:
            self.d.close()
        except Exception:
            pass


# ---------------------------------------------------------------- CLI
def main(argv=None):
    p = argparse.ArgumentParser(description="Dymola 无窗口驱动")
    p.add_argument("--smoke", action="store_true", help="检测 Dymola 可用性")
    p.add_argument("--model", help="要翻译/仿真的完整类名")
    p.add_argument("--open", dest="openfile", help="先 openModel 的 .mo 文件")
    p.add_argument("--workdir")
    p.add_argument("--translate", action="store_true")
    p.add_argument("--simulate", action="store_true")
    p.add_argument("--final", default="")
    p.add_argument("--stop", type=float, default=1.0)
    p.add_argument("--dymolapath", default="")
    p.add_argument("--msvc-dir", default=None)
    p.add_argument("--out", help="统计/结果写出 JSON")
    args = p.parse_args(argv)

    try:
        drv = Driver(args.dymolapath, args.msvc_dir)
    except DymolaUnavailable as e:
        print(f"dymola-unavailable: {e}")
        return 3

    print(f"Dymola {drv.version}; compiler_ok={drv.compiler_ok}")
    if args.smoke and not args.model:
        drv.close()
        return 0
    if args.openfile:
        drv.d.openModel(args.openfile, changeDirectory=False)
    result = {}
    if args.translate:
        result["translate"] = drv.translate(args.model, args.workdir)
        print("translate ok:", result["translate"]["ok"],
              "stats:", result["translate"]["stats"])
    if args.simulate:
        fn = [s for s in args.final.split(",") if s]
        result["simulate"] = drv.simulate(args.model, args.stop, fn, workdir=args.workdir)
        print("simulate ok:", result["simulate"]["ok"], "final:", result["simulate"]["final"])
    drv.close()
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
    return 0


_SAMPLE = """\
The DAE has 106 scalar unknowns and 106 scalar equations.
Statistics
Original Model
Number of components: 14
Continuous time states: 8 scalars
Sizes of linear systems of equations: {13}
Sizes of nonlinear systems of equations: { }
"""


def _selftest():
    s = parse_translation_stats(_SAMPLE)
    assert s["scalar_unknowns"] == 106 and s["scalar_equations"] == 106, s
    assert s["components"] == 14, s
    assert s["continuous_states"] == 8, s
    assert s["linear_sizes"] == [13], s
    assert s["nonlinear_sizes"] == [], s
    print("dymola_driver stats self-test: passed")


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        _selftest()
    else:
        sys.exit(main())
