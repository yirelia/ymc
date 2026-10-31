#!/usr/bin/env python3
"""S5 准入普查（ADR-008 D2）：读 bdae_refgen.py 的签名 JSONL，出 A2 / A3 / A4 的表。

  python -I tools/bdae_census.py <sig.jsonl> [--a1 <a1.jsonl>] [--top 12] [--json out.json]

- 拒绝的归类顺序：没跑到 Bindings（无 `Number of …` 计数，A2 无从判）→ A3；有计数且 N != n → A2；其余 A3 失败 → A3；都过 → 准入。
- A4 的恒等有两种：text = 段体逐字相同；sig = 5a 签名相同（表达式变了但桶和计数没变）。
- Simplify 1 / 2 是同一个函数（NBackendDAE.mo:270-275 的 `simplify`），实现上算一个单元。

基建工具，Claude 提供。
"""
from __future__ import annotations

import argparse
import collections
import itertools
import json
import re

MODULES = ("Bindings", "FunctionAlias", "Early Inline", "Simplify 1", "Alias", "Simplify 2", "Remove Stream",
           "Detect States", "Events")
UNITS = {"Bindings": ("Bindings",), "FunctionAlias": ("FunctionAlias",), "Early Inline": ("Early Inline",),
         "Simplify": ("Simplify 1", "Simplify 2"), "Alias": ("Alias",), "Remove Stream": ("Remove Stream",),
         "Detect States": ("Detect States",), "Events": ("Events",)}
UNIT_ORDER = tuple(UNITS)
# NB 运行序里有数据依赖的对：Simplify 在 Alias 之前（Alias 吃简化后的方程），Detect States 在 Events 之前（NBackendDAE.mo:270-278）。
CHAIN_BEFORE = (("Simplify", "Alias"), ("Detect States", "Events"))


def load(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(ln) for ln in f if ln.strip()]


def shape_ok(r):
    """收紧的 A2'：(After Lowering) 里没有数组变量（任何桶）、Record 桶为空、没有 [ARRY] / [FOR-] / [RECD] / [TUPL]。

    ADR 的 N == n 判不准：Variables 只数 unknowns（NBVariable.mo:VarData.size），parameter / constant 数组、size-1 数组、
    字段全标量的记录都能过；反过来多输出的 when / if / algorithm 标量方程会被 Equations 的 N != n 误挡。"""
    s = r.get("shape")
    return bool(s) and not any(s.get(k) for k in ("array_vars", "array_eqs", "record_vars", "record_eqs"))


def classify(r, strict=False):
    """返回 admitted / a2 / a3-timeout / a3-error / a3-other / tool-error。strict 时 A2 换成 A2'（shape_ok）。"""
    if r.get("status") in ("tool-error", "skipped-annotation"):
        return "tool-error"
    if r.get("status") in ("timeout", "oversize"):
        return "a3-timeout"
    if not r.get("counts"):
        return "a3-error" if r.get("errors") else "a3-other"
    if not (shape_ok(r) if strict else r.get("a2")):
        return "a2"
    if r.get("a3"):
        return "admitted"
    return "a3-error" if r.get("errors") else "a3-other"


def needs(r, crit):
    """这个模型上非恒等的实现单元集合。"""
    out = set()
    for u, mods in UNITS.items():
        for m in mods:
            if m not in r.get("identity", {}) or not r["identity"][m][crit]:
                out.add(u)
    return frozenset(out)


def norm_err(e):
    e = re.sub(r"^\[[^\]]*\] ", "", e)
    e = re.sub(r"(?<![\w.])(?!NB|NF)\$?[A-Za-z_][\w$]*\.[\w.$]+", "<cref>", e)      # 折叠模型里的点名，保留 NB* / NF* 函数名
    e = re.sub(r"\d+", "N", e)
    return e[:110]


def greedy_table(need_sets, total):
    """逐步选单元，使每步累计准入最多（平局按 NB 序）。返回 [(单元, 累计)]，第 0 行是 lower only。"""
    rows, impl = [("lower only", sum(1 for s in need_sets if not s))], set()
    left = list(UNIT_ORDER)
    while left:
        best = max(left, key=lambda u: (sum(1 for s in need_sets if s <= impl | {u}), -UNIT_ORDER.index(u)))
        impl.add(best)
        left.remove(best)
        rows.append((best, sum(1 for s in need_sets if s <= impl)))
    return rows


def best_chain(need_sets):
    """穷举 8! 个实现序，取累计准入面积最大、且守 CHAIN_BEFORE 的那条（模型早进门早受益）。"""
    cnt = collections.Counter(need_sets)
    items = list(cnt.items())
    best, best_area = None, -1
    for perm in itertools.permutations(UNIT_ORDER):
        pos = {u: i for i, u in enumerate(perm)}
        if any(pos[a] > pos[b] for a, b in CHAIN_BEFORE):
            continue
        area, impl, rows = 0, set(), []
        for u in perm:
            impl.add(u)
            k = sum(c for s, c in items if s <= impl)
            area += k
            rows.append((u, k))
        if area > best_area:
            best, best_area = rows, area
    return [("lower only", cnt.get(frozenset(), 0))] + best


def prefix_table(need_sets):
    """严格按 NB 运行序实现时的累计。"""
    rows, impl = [("lower only", sum(1 for s in need_sets if not s))], set()
    for u in UNIT_ORDER:
        impl.add(u)
        rows.append((u, sum(1 for s in need_sets if s <= impl)))
    return rows


def hist(recs, field):
    models, total = collections.Counter(), collections.Counter()
    for r in recs:
        for k, v in r.get("shape", {}).get(field, {}).items():
            models[k] += 1
            total[k] += v
    return [(k, models[k], total[k]) for k in sorted(models, key=lambda x: -models[x])]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sig")
    ap.add_argument("--a1")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--json")
    ap.add_argument("--strict", action="store_true", help="A2 换成 A2'（见 shape_ok）")
    a = ap.parse_args()
    recs = load(a.sig)
    a1 = {}
    if a.a1:
        a1 = {r["id"]: r for r in load(a.a1)}
    print(f"total {len(recs)}  ({'strict A2prime' if a.strict else 'ADR A2'})")
    cls = collections.Counter()
    for r in recs:
        r["class"] = classify(r, a.strict)
        cls[r["class"]] += 1
    # 门槛分层：不要求 A3 时，各阶段的参考段是否在手（标量形状）
    tier = [r for r in recs if shape_ok(r) and (not a1 or a1.get(r["id"], {}).get("a1"))]
    print(f"tiers on A2' shape{' + A1' if a1 else ''}: has (After Lowering) {len(tier)}, "
          f"has (Events) {sum(1 for r in tier if 'Events' in r.get('stages', []))}, "
          f"has first Causalize {sum(1 for r in tier if r.get('sig_5b'))}, "
          f"A3 ok {sum(1 for r in tier if r.get('a3'))}")
    if a1:
        n = sum(1 for r in recs if not a1.get(r["id"], {}).get("a1"))
        print(f"A1 not met (ymc flat != omc flat, or ymc skip): {n}")
        print("  " + str(collections.Counter(a1.get(r['id'], {}).get('why', 'missing') for r in recs)))
    print("classes:", dict(cls))
    adm = [r for r in recs if r["class"] == "admitted"]
    if a1:
        both = [r for r in adm if a1.get(r["id"], {}).get("a1")]
        print(f"admitted by A2+A3: {len(adm)}; of these A1 ok: {len(both)}")
        adm = both

    # A2 细分
    a2 = [r for r in recs if r["class"] == "a2"]
    shape = collections.Counter()
    for r in a2:
        s = r.get("shape", {})
        if not r["a2_var"] and not r["a2_eq"]:
            shape["var+eq N!=n"] += 1
        elif not r["a2_var"]:
            shape["var only N!=n"] += 1
        else:
            shape["eq only N!=n"] += 1
        arr = s.get("array_vars", 0) or s.get("array_eqs", 0)
        rec = s.get("record_vars", 0) or s.get("record_eqs", 0)
        if not arr and not rec:
            shape["no array/record in After Lowering (false reject?)"] += 1
            if s.get("multi_size_eqs", 0):
                shape["  ...with multi-size when/if/algorithm"] += 1
    print("A2 rejects:", dict(shape))
    print("A2 reject also A3 fail:", sum(1 for r in a2 if not r.get("a3")), "/", len(a2))
    # 准入集里 A2 没挡住的非标量痕迹
    leak = collections.Counter()
    for r in adm:
        s = r.get("shape", {})
        for k in ("array_vars", "record_vars", "array_eqs", "record_eqs", "multi_size_eqs"):
            if s.get(k):
                leak[k] += 1
    print("admitted but shape has:", dict(leak))

    # A3 细分
    for key in ("a3-error", "a3-other", "a3-timeout"):
        rs = [r for r in recs if r["class"] == key]
        print(f"\n{key}: {len(rs)}")
        c = collections.Counter(norm_err(r["errors"][0]) if r.get("errors") else (r.get("status") + " rc=" + str(r.get("rc")))
                                for r in rs)
        for e, n in c.most_common(a.top):
            print(f"  {n:5d}  {e}")
        c2 = collections.Counter(r.get("last_section", "") or "<none>" for r in rs)
        print("  last section:", c2.most_common(6))
    # 有计数但 A3 失败：还剩多少段可用
    a3f = [r for r in recs if r.get("counts") and not r.get("a3")]
    print(f"\nreached Bindings but A3 failed: {len(a3f)}; "
          f"with (Events) {sum(1 for r in a3f if 'Events' in r.get('stages', []))}, "
          f"with first Causalize {sum(1 for r in a3f if r.get('sig_5b'))}, "
          f"with stats {sum(1 for r in a3f if r.get('stats_printed'))}")

    print(f"\n== A4 on {len(adm)} admitted ==")
    print(f"{'module':16s} {'non-id text':>12s} {'non-id sig':>11s}")
    non = {}
    for m in MODULES:
        t = sum(1 for r in adm if not r.get("identity", {}).get(m, {}).get("text", False))
        s = sum(1 for r in adm if not r.get("identity", {}).get(m, {}).get("sig", False))
        non[m] = (t, s)
        print(f"{m:16s} {t:12d} {s:11d}")
    out = {"classes": dict(cls), "nonidentity": non}
    for crit in ("text", "sig"):
        sets = [needs(r, crit) for r in adm]
        print(f"\n-- cumulative ({crit} identity) --")
        print("  greedy (each step best gain):")
        for u, k in greedy_table(sets, len(adm)):
            print(f"    {u:16s} {k:6d}")
        print("  best chain (max area; Simplify<Alias, Detect States<Events):")
        for u, k in best_chain(sets):
            print(f"    {u:16s} {k:6d}")
        print("  NB run order:")
        for u, k in prefix_table(sets):
            print(f"    {u:16s} {k:6d}")
        c = collections.Counter(sets)
        print("  top need-sets:")
        for s, n in c.most_common(a.top):
            print(f"    {n:5d}  {sorted(s, key=UNIT_ORDER.index)}")
        out["needsets_" + crit] = [[sorted(s), n] for s, n in c.most_common()]

    print(f"\n== equation-kind classes over {len(adm)} admitted ==")
    kinds = collections.Counter()
    for r in adm:
        tags = sorted(r.get("shape", {}).get("eq_tags", {}))
        n = r.get("counts", {}).get("variables", [0])[0]
        kinds[(",".join(tags) or "<no equations>") + (" | 0 unknowns" if n == 0 else "")] += 1
    for k, n in kinds.most_common():
        print(f"  {n:5d}  {k}")
    print(f"\n== (After Lowering) shape over {len(adm)} admitted (models, occurrences) ==")
    for f in ("var_tags", "eq_tags", "nested", "calls"):
        print(f"{f}:")
        rows = hist(adm, f)
        for k, m, t in rows[:40]:
            print(f"  {k:32s} {m:5d} {t:7d}")
    if a.json:
        out["admitted"] = [r["id"] for r in adm]
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False, indent=1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
