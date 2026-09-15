"""报告与三方差异台账（FR-040/041/042）。

- report.json（IF-004）：由 run.py 直接产出；本模块负责渲染 report.md、生成待办块、
  以及三方差异台账 ledger.json（IF-006）的读写。
- 台账：ymc/OMC/Dymola 三者不一致时记录；按用户决定 2，三方不一致本身不算 ymc 失败。
- 待办块：把 FAIL 汇总成可粘贴进 tasks/todo.md 的 Markdown 复选框（不自动改 todo.md）。

基建工具，Claude 提供。
"""
from __future__ import annotations

import argparse
import json
import os

LEDGER_CATEGORIES = {
    "omc-dym-disagree", "ymc-only-fail", "var-unmatched", "ref-stale", "known-nonequiv",
}


# ---------------------------------------------------------------- report.md
def render_md(report):
    s = report.get("summary", {})
    total = s.get("pass", 0) + s.get("fail", 0) + s.get("skip", 0)
    lines = [f"# ymc 对比报告：{report.get('stage', '?')}（tool={report.get('tool', '?')}）", ""]
    lines.append(f"- 合计：{total}；PASS {s.get('pass', 0)}；FAIL {s.get('fail', 0)}；SKIP {s.get('skip', 0)}")
    rate = (s.get("pass", 0) / total * 100) if total else 0.0
    lines.append(f"- 通过率：{rate:.1f}%")
    lines.append("")
    by_dir = s.get("by_dir", {})
    if by_dir:
        lines.append("## 按目录")
        lines.append("")
        lines.append("| 目录 | PASS | FAIL | SKIP |")
        lines.append("|---|---|---|---|")
        for d in sorted(by_dir):
            b = by_dir[d]
            lines.append(f"| {d} | {b.get('pass', 0)} | {b.get('fail', 0)} | {b.get('skip', 0)} |")
        lines.append("")
    fails = [c for c in report.get("cases", []) if c["verdict"] == "FAIL"]
    if fails:
        lines.append("## 失败明细")
        lines.append("")
        for c in fails:
            lines.append(f"- `{c['path']}`（{c.get('compare_kind', '')}）：{c.get('fail_summary', '')}")
        lines.append("")
    skips = {}
    for c in report.get("cases", []):
        if c["verdict"] == "SKIP":
            skips[c.get("skip_reason", "?")] = skips.get(c.get("skip_reason", "?"), 0) + 1
    if skips:
        lines.append("## SKIP 原因分布")
        lines.append("")
        for reason, n in sorted(skips.items(), key=lambda kv: -kv[1]):
            lines.append(f"- {reason}: {n}")
        lines.append("")
    return "\n".join(lines)


def todo_block(report):
    fails = [c for c in report.get("cases", []) if c["verdict"] == "FAIL"]
    if not fails:
        return "（无失败用例）"
    by_dir = {}
    for c in fails:
        by_dir.setdefault(os.path.dirname(c["path"]), []).append(c)
    out = [f"## {report.get('stage', '?')} 失败待办（{len(fails)} 项）", ""]
    for d in sorted(by_dir):
        out.append(f"### {d}")
        for c in by_dir[d]:
            out.append(f"- [ ] `{c['path']}` — {c.get('fail_summary', '')}")
        out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------- ledger
def load_ledger(path):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {"schema_version": 1, "entries": []}


def add_ledger_entry(path, model, category, values=None, verdicts=None,
                     var=None, note="", when=""):
    if category not in LEDGER_CATEGORIES:
        raise ValueError(f"未知台账分类: {category}")
    led = load_ledger(path)
    entry = {"model": model, "category": category, "values": values or {},
             "verdicts": verdicts or {}, "first_seen": when, "note": note}
    if var:
        entry["var"] = var
    # 去重：同 model+var+category 只留一条
    key = (model, var, category)
    led["entries"] = [e for e in led["entries"]
                      if (e["model"], e.get("var"), e["category"]) != key]
    led["entries"].append(entry)
    led["entries"].sort(key=lambda e: (e["model"], e.get("var") or "", e["category"]))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(led, f, ensure_ascii=False, indent=1, sort_keys=True)
    return entry


def main(argv=None):
    p = argparse.ArgumentParser(description="报告渲染与台账")
    p.add_argument("--in", dest="inp", help="report.json 路径")
    p.add_argument("--md", help="写出 report.md 到此路径")
    p.add_argument("--todo-block", action="store_true", help="打印可粘贴的待办块")
    args = p.parse_args(argv)
    if not args.inp:
        p.print_help()
        return 2
    with open(args.inp, "r", encoding="utf-8") as f:
        report = json.load(f)
    if args.md:
        with open(args.md, "w", encoding="utf-8") as f:
            f.write(render_md(report))
        print(f"已写 {args.md}")
    if args.todo_block:
        print(todo_block(report))
    if not args.md and not args.todo_block:
        print(render_md(report))
    return 0


def _selftest():
    rep = {"stage": "S1", "tool": "ymc",
           "summary": {"pass": 8, "fail": 2, "skip": 1,
                       "by_dir": {"flattening/x": {"pass": 8, "fail": 2, "skip": 1}}},
           "cases": [
               {"path": "flattening/x/a.mo", "verdict": "FAIL", "compare_kind": "L1",
                "fail_summary": "Line 3: text differs"},
               {"path": "flattening/x/b.mo", "verdict": "SKIP", "skip_reason": "old-frontend"},
           ]}
    md = render_md(rep)
    assert "通过率" in md and "a.mo" in md
    tb = todo_block(rep)
    assert "[ ]" in tb and "a.mo" in tb
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        lp = os.path.join(d, "ledger.json")
        add_ledger_entry(lp, "M.Foo", "omc-dym-disagree",
                         values={"omc": 1.0, "dymola": 1.2}, note="事件时刻不同")
        led = load_ledger(lp)
        assert led["entries"][0]["category"] == "omc-dym-disagree"
    print("report self-test: passed")


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv:
        _selftest()
    else:
        sys.exit(main())
