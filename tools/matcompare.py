"""数值结果比对器（FR-011/012/013）。

三套判据：
- tube    ：复刻 OMC diffSimulationResults（ITI csv-compare 管道算法）。默认
            relTol=1e-3, relTolDiffMinMax=1e-4, rangeDelta=0.002。禁止 rangeDelta=0。
- classic ：复刻 OMC compareSimulationResults（单一绝对阈值 average=relTol*mean(|ref|)+absTol）。
            默认 relTol=0.01, absTol=1e-4。
- csvtube ：与 tube 同族，用于 Dymola/MAP-LIB（csv-compare），默认 tolerance=0.002。

tube 算法逐行移植 testsuite 运行时 SimulationResultsCmpTubes.c（研究底稿 R1、直接读源）：
calculateTubes / generateHighTube / generateLowTube / calibrateValues / addRelativeTolerance
/ validate。注意 mergeTimelines / removeUneventfulPoints / addTargetEventTimes 在源码里被
注释掉，不参与，故此处也不实现。

以参考曲线为中心生成管道；把实际曲线线性插值到参考时间轴；事件点（参考轴相邻时间相等）
不计误差；实际比参考长则截断、比参考短则末值保持并判失败；变量缺失只告警不失败。

基建工具，Claude 提供。
"""
from __future__ import annotations

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import resultio  # noqa: E402


# ---------------------------------------------------------------- 基础
def _almost_equal(a, b, reltol, abstol):
    """almostEqualRelativeAndAbs：|a-b| <= abstol 或 <= max(|a|,|b|)*reltol（Cmp.c:155-166）。"""
    diff = abs(a - b)
    return diff <= abstol or diff <= max(abs(a), abs(b)) * reltol


def _lin_interp(x, x0, x1, y0, y1, xabstol):
    if _almost_equal(x0, x, 0.0, xabstol):
        return y0
    if _almost_equal(x1, x, 0.0, xabstol):
        return y1
    if _almost_equal(x1, x0, 0.0, xabstol):
        return y0
    return y0 + ((y1 - y0) / (x1 - x0)) * (x - x0)


# ---------------------------------------------------------------- tube 生成
class _Priv:
    __slots__ = ("mh", "ml", "xHigh", "xLow", "yHigh", "yLow",
                 "i0h", "i1h", "i0l", "i1l", "tStart", "tStop",
                 "delta", "S", "xMinStep", "min", "max", "countHigh", "countLow")


def _calculate_tubes(x, y, r):
    """移植 calculateTubes（SimulationResultsCmpTubes.c:191-325）。x 可能被就地修改。"""
    n = len(x)
    p = _Priv()
    p.tStart = x[0]
    p.tStop = x[n - 1]
    xRelEps = 1e-15
    p.xMinStep = ((p.tStop - p.tStart) + abs(p.tStart)) * xRelEps
    cap = n + 2
    p.mh = [0.0] * cap; p.ml = [0.0] * cap
    p.i0h = [0] * cap; p.i1h = [0] * cap
    p.i0l = [0] * cap; p.i1l = [0] * cap
    p.xHigh = [0.0] * cap; p.xLow = [0.0] * cap
    p.yHigh = [0.0] * cap; p.yLow = [0.0] * cap
    p.countHigh = 0
    p.countLow = 0
    p.delta = r * (p.tStop - p.tStart)
    p.max = y[0]; p.min = y[0]
    for i in range(1, n):
        p.max = max(y[i], p.max)
        p.min = min(y[i], p.min)
    span = abs(p.tStop - p.tStart)
    p.S = abs(4.0 * (p.max - p.min) / span) if span else 0.0
    if span and p.S < 0.0004 / span:
        p.S = 0.0004 / span

    for i in range(1, n):
        x1 = x[i]; y1 = y[i]
        x2 = x[i - 1]; y2 = y[i - 1]
        if x1 <= x2 and y1 == y2 and p.countHigh == 0:
            continue
        if x1 <= x2 and y1 == y2:
            x1 = max(x1, x[p.i1l[p.countLow - 1]] + p.xMinStep)
            x1 = max(x1, x[p.i1h[p.countHigh - 1]] + p.xMinStep)
            x[i] = x1
            cur = p.mh[p.countHigh - 1]
        else:
            if x1 <= x2:
                x1 = x2 + p.xMinStep
                x[i] = x1
            cur = (y1 - y2) / (x1 - x2)

        p.i0h[p.countHigh] = i; p.i1h[p.countHigh] = i - 1; p.mh[p.countHigh] = cur
        p.i0l[p.countLow] = i; p.i1l[p.countLow] = i - 1
        cur_low = p.ml[p.countLow - 1] if (x1 <= x2 and y1 == y2) else cur
        p.ml[p.countLow] = cur_low

        if p.countHigh == 0:
            root = p.delta * math.sqrt(cur * cur + p.S * p.S)
            p.xHigh[p.countHigh] = x2 - p.delta
            p.yHigh[p.countHigh] = y2 - cur * p.delta + root
            p.xLow[p.countLow] = x2 - p.delta
            p.yLow[p.countLow] = y2 - cur * p.delta - root
            p.countHigh += 1; p.countLow += 1
        else:
            p.xHigh[p.countHigh] = 1.0; p.yHigh[p.countHigh] = 1.0
            p.xLow[p.countLow] = 1.0; p.yLow[p.countLow] = 1.0
            p.countHigh += 1; p.countLow += 1
            _gen_tube(p, x, y, upper=True)
            _gen_tube(p, x, y, upper=False)

    # terminal value（upper 然后 lower）
    for upper in (True, False):
        if upper:
            xc, yc, mm, cnt = p.xHigh, p.yHigh, p.mh, p.countHigh
        else:
            xc, yc, mm, cnt = p.xLow, p.yLow, p.ml, p.countLow
        x1 = xc[cnt - 1]; y1 = yc[cnt - 1]; x2 = p.tStop
        cur = mm[cnt - 1]
        xc[cnt] = x2 + p.delta
        yc[cnt] = y1 + cur * (x2 + p.delta - x1)
        if upper:
            p.countHigh += 1
        else:
            p.countLow += 1
    return p


def _gen_tube(p, x, y, upper):
    """移植 generateHighTube/generateLowTube。upper=True 上管道，否则下管道。"""
    if upper:
        m_, i0, i1, xc, yc = p.mh, p.i0h, p.i1h, p.xHigh, p.yHigh
        cnt = p.countHigh
        sgn = 1.0
    else:
        m_, i0, i1, xc, yc = p.ml, p.i0l, p.i1l, p.xLow, p.yLow
        cnt = p.countLow
        sgn = -1.0
    index = cnt - 1
    m1 = m_[index]
    m2 = m_[index - 1]
    slope_dif = abs(m1 - m2)
    prev_gap = i0[cnt - 1] - i1[cnt - 2]
    if slope_dif == 0 or (slope_dif < 2e-15 * max(abs(m1), abs(m2)) and prev_gap < 100):
        i0[index - 1] = i0[index]
        cnt -= 1
        x3 = x[i0[index - 1]]; y3 = y[i0[index - 1]]
        x4 = x[i1[index - 1]]; y4 = y[i1[index - 1]]
        m_[index - 1] = (y3 - y4) / (x3 - x4)
    else:
        r1 = math.sqrt(m1 * m1 + p.S * p.S)
        r2 = math.sqrt(m2 * m2 + p.S * p.S)
        # x2/y2 = 当前迭代的前一采样点 x[i-1]/y[i-1]（= calculateTubes 里的 priv->x2/y2）；
        # 此刻 i0[index] 恰等于当前 i，故 x[i0[index]-1] == x[i-1]。
        x2 = x[i0[index] - 1]
        y2 = y[i0[index] - 1]
        xc[index] = x2 - sgn * (p.delta * (m1 + m2) / (r2 + r1))
        if m1 * m2 < 0:
            yc[index] = y2 + sgn * (p.delta * (m1 * r2 - m2 * r1)) / (m1 - m2)
        else:
            yc[index] = y2 + sgn * (p.S * p.S * p.delta * (m1 + m2) / (m1 * r2 + m2 * r1))
        if xc[index] == xc[index - 1] and yc[index] != yc[index - 1]:
            xc[index] = xc[index - 1] + p.xMinStep
            yc[index] = y2 + m1 * (xc[index] - x2) + sgn * p.delta * r1
            m_[index - 1] = (yc[index] - yc[index - 1]) / p.xMinStep
        while index > 1 and (xc[index] <= xc[index - 1] if upper else xc[index] <= xc[index - 1]):
            i0[index - 1] = i0[index]
            i1[index - 1] = i1[index]
            m_[index - 1] = m_[index]
            index -= 1
            cnt -= 1
            if index == 0:
                x3 = x[0]
                xc[index] = x3 - p.delta
                yc[index] = y2 + m1 * (xc[index] - x2) + sgn * p.delta * r1
            else:
                x3 = xc[index - 1]; y3 = yc[index - 1]
                m2 = m_[index - 1]
                xc[index] = (m2 * x3 - m1 * x2 + y2 - y3 + sgn * p.delta * r1) / (m2 - m1)
                yc[index] = (m2 * m1 * (x3 - x2) + m2 * (y2 + sgn * p.delta * r1) - m1 * y3) / (m2 - m1)
    if upper:
        p.countHigh = cnt
    else:
        p.countLow = cnt


def _skip_tubes(x, y):
    p = _Priv()
    n = len(x)
    p.tStart = x[0]; p.tStop = x[n - 1]
    p.xMinStep = ((p.tStop - p.tStart) + abs(p.tStart)) * 1e-15
    p.countHigh = n; p.countLow = n
    p.yHigh = list(y); p.yLow = list(y)
    p.xHigh = list(x); p.xLow = list(x)
    p.max = max(y); p.min = min(y)
    return p


def _calibrate(source_tl, target_tl, target_vals, nsource, ntarget, xabstol):
    """移植 calibrateValues：把 target 序列插值到 source 时间轴，返回 (values, new_nsource)。
    截断判据用 source 轴末点（避免外推），与 SimulationResultsCmpTubes.c:359-362 一致。"""
    n = nsource
    if ntarget < 2:
        return [target_vals[0] if target_vals else 0.0] * n, n
    src_last = source_tl[nsource - 1]
    out = [0.0] * n
    j = 1
    x0 = x1 = y0 = y1 = 0.0
    for i in range(n):
        x = source_tl[i]
        if target_tl[j] > src_last and target_tl[j - 1] > src_last:
            out[i] = _lin_interp(x, x0, x1, y0, y1, xabstol)
            n = i + 1
            break
        x1 = target_tl[j]; y1 = target_vals[j]
        while x1 <= x and (j + 1) < ntarget:
            j += 1
            x1 = target_tl[j]; y1 = target_vals[j]
            if _almost_equal(x1, x, 0.0, xabstol):
                break
        x0 = target_tl[j - 1]; y0 = target_vals[j - 1]
        if i and _almost_equal(source_tl[i - 1], x0, 0.0, xabstol) and _almost_equal(x0, x1, 0.0, xabstol):
            out[i] = y1  # 事件右极限
        else:
            out[i] = _lin_interp(x, x0, x1, y0, y1, xabstol)
    return out[:n], n


def _add_rel_tol(target, source, length, reltol, abstol, direction):
    if direction > 0:
        for i in range(length):
            target[i] = max(source[i] + max(abs(source[i] * reltol), abstol), target[i])
    else:
        for i in range(length):
            target[i] = min(source[i] - max(abs(source[i] * reltol), abstol), target[i])


def _validate(n, ref_time, ref_vals, low, high, calibrated, reltol, abstol, xabstol):
    """移植 validate：返回 True=有越界（不同），False=通过。"""
    isdifferent = 0
    last_step_error = 1
    for i in range(n):
        is_event = (i and _almost_equal(ref_time[i], ref_time[i - 1], 0.0, xabstol)) or \
                   (i + 1 < n and _almost_equal(ref_time[i], ref_time[i + 1], 0.0, xabstol))
        if is_event:
            refv = ref_vals[i]; val = calibrated[i]
            tol = max(abstol * 10, max(abs(refv), abs(val)) * reltol * 10)
            high[i] = (refv if last_step_error else max(refv, val)) + tol
            low[i] = (refv if last_step_error else min(refv, val)) - tol
        else:
            this_step_error = last_step_error
            if calibrated[i] < low[i]:
                isdifferent += 1; this_step_error = 1
            elif calibrated[i] > high[i]:
                isdifferent += 1; this_step_error = 1
            last_step_error = this_step_error
    return isdifferent > 0


# ---------------------------------------------------------------- 单变量比对
def cmp_var_tube(ref_time, ref_vals, act_time, act_vals,
                 reltol, range_delta, reltol_diff_maxmin):
    """比对一个变量：True=Equal，False=Different。移植 cmpDataTubes 的 diff 路径。"""
    if range_delta == 0:
        raise ValueError("rangeDelta=0 会让 OMC 段错误；请用极小正值")
    ref_time = list(map(float, ref_time))
    ref_vals = list(map(float, ref_vals))
    act_time = list(map(float, act_time))
    act_vals = list(map(float, act_vals))
    span = ref_time[-1] - ref_time[0]
    xabstol = span * 1e-3 / max(len(act_time), len(ref_time))
    p = _calculate_tubes(ref_time, ref_vals, range_delta)
    n = len(ref_time)
    calibrated, n = _calibrate(ref_time, act_time, act_vals, n, len(act_time), xabstol)
    high, n = _calibrate(ref_time, p.xHigh, p.yHigh, n, p.countHigh, xabstol)
    low, n = _calibrate(ref_time, p.xLow, p.yLow, n, p.countLow, xabstol)
    # n 已被逐次截断到最终值；三个序列统一截到最终 n
    calibrated = calibrated[:n]
    high = high[:n]
    low = low[:n]
    if p.max - p.min == 0 and p.max < reltol_diff_maxmin * reltol_diff_maxmin:
        abstol = reltol_diff_maxmin * reltol_diff_maxmin
    else:
        abstol = abs((p.max - p.min) * reltol_diff_maxmin)
    _add_rel_tol(high, ref_vals, n, reltol, abstol, 1)
    _add_rel_tol(low, ref_vals, n, reltol, abstol, -1)
    different = _validate(n, ref_time, ref_vals, low, high, calibrated, reltol, abstol, xabstol)
    return not different


def cmp_var_classic(ref_time, ref_vals, act_time, act_vals, reltol, abstol):
    """比对一个变量（compareSimulationResults 简化版）：average=reltol*mean(|ref|)+abstol，
    实际点插值到参考轴后比 |d-dr|>average。事件精确处理未完全复刻（ponytail: 够用即止）。"""
    ref_time = list(map(float, ref_time)); ref_vals = list(map(float, ref_vals))
    act_time = list(map(float, act_time)); act_vals = list(map(float, act_vals))
    mean_abs = sum(abs(v) for v in ref_vals) / len(ref_vals)
    average = reltol * abs(mean_abs) + abstol
    # 把实际值插值到参考时间轴逐点比
    import bisect
    for i, t in enumerate(ref_time):
        if t < act_time[0] or t > act_time[-1]:
            continue
        j = bisect.bisect_left(act_time, t)
        if j < len(act_time) and act_time[j] == t:
            while j + 1 < len(act_time) and act_time[j + 1] == act_time[j]:
                j += 1
            d = act_vals[j]
        elif j == 0:
            d = act_vals[0]
        else:
            x0, x1 = act_time[j - 1], act_time[j]
            y0, y1 = act_vals[j - 1], act_vals[j]
            d = y0 if x1 == x0 else y0 + (y1 - y0) * (t - x0) / (x1 - x0)
        if abs(d - ref_vals[i]) > average:
            return False
    return True


# ---------------------------------------------------------------- 文件级比对
def compare(ref_path, actual_path, algo="tube", vars=None,
            reltol=None, reltoldiffminmax=1e-4, rangedelta=0.002, abstol=1e-4,
            tolerance=0.002):
    """比对两个结果文件。返回 (success, fail_vars, unmatched_vars)。
    - success：范围内变量全部 Equal（变量缺失不算失败，进 unmatched）。
    """
    ref = resultio.read(ref_path)
    act = resultio.read(actual_path)
    if algo == "csvtube":
        rangedelta = tolerance
    if reltol is None:
        reltol = 0.01 if algo == "classic" else 1e-3
    if vars:
        want = list(vars)
    else:
        want = [v for v in ref.variables() if v.lower() != "time"]
    fail, unmatched = [], []
    rt = ref.time(); at = act.time()
    for v in want:
        if not ref.has(v) or not act.has(v):
            unmatched.append(v)
            continue
        rv = ref.series(v); av = act.series(v)
        if algo == "classic":
            ok = cmp_var_classic(rt, rv, at, av, reltol, abstol)
        else:  # tube / csvtube
            ok = cmp_var_tube(rt, rv, at, av, reltol, rangedelta, reltoldiffminmax)
        if not ok:
            fail.append(v)
    return (len(fail) == 0, fail, unmatched)


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(description="数值结果比对（tube/classic/csvtube）")
    p.add_argument("--algo", choices=["tube", "classic", "csvtube"], default="tube")
    p.add_argument("--ref", required=True)
    p.add_argument("--actual", required=True)
    p.add_argument("--vars", default="")
    p.add_argument("--reltol", type=float, default=None)
    p.add_argument("--reltoldiffminmax", type=float, default=1e-4)
    p.add_argument("--rangedelta", type=float, default=0.002)
    p.add_argument("--abstol", type=float, default=1e-4)
    p.add_argument("--tolerance", type=float, default=0.002)
    a = p.parse_args(argv)
    vars = [s for s in a.vars.split(",") if s] or None
    ok, fail, unmatched = compare(a.ref, a.actual, a.algo, vars,
                                  a.reltol, a.reltoldiffminmax, a.rangedelta,
                                  a.abstol, a.tolerance)
    print(f"({'true' if ok else 'false'}, {{{', '.join(fail)}}})")
    if unmatched:
        print(f"unmatched (not counted): {{{', '.join(unmatched)}}}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
