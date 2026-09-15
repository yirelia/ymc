"""仿真结果读取器：MATLAB v4 `.mat`（OMC/Dymola 通用）、`.csv`、`.mat.xz`（FR-010）。

布局（研究底稿 R4；本机 OMC 1.25.0 实测确认）：
- 6 个矩阵 Aclass/name/description/dataInfo/data_1/data_2。
- dataInfo 4×nSignals：row0=数据集(1→data_1 参数/常量; 0 或 2→data_2 时变量)，
  row1=有符号 1 基行号（负号=取反别名），row2=插值，row3=外推。
- data_1 nRows×2：col0/col1=start/stop（参数取 col0）；row0 保留 [tStart,tStop]。
- data_2 nSignals×nPoints：row0=time，其余为各变量时间序列（列=时间点）。
- 变量在事件重复时刻取右极限（与 OMC val() 一致，R1§7）。

依赖：scipy.io.loadmat（读 MAT v4）、numpy、外部 xz（解压 .mat.xz）。
基建工具，Claude 提供。
"""
from __future__ import annotations

import csv as _csv
import os
import subprocess
import tempfile

import numpy as np
import scipy.io


class Result:
    """一个仿真结果文件的只读视图。"""

    def __init__(self, names, series, time, is_param):
        self._names = list(names)                 # 变量名（顺序 = 文件内顺序）
        self._series = series                     # name -> np.ndarray（与 _time 等长）
        self._time = np.asarray(time, dtype=float)
        self._is_param = is_param                 # name -> bool

    # ---- 查询 ----
    def variables(self):
        return list(self._names)

    def has(self, name):
        return name in self._series

    def time(self):
        return self._time

    def series(self, name):
        return self._series[name]

    def final(self, name):
        return float(self._series[name][-1])

    def is_param(self, name):
        return self._is_param.get(name, False)

    def val(self, name, t):
        """在时刻 t 取值：线性插值；事件重复时刻取右极限；越界返回 NaN
        （对齐 OMC val()，R1§7）。参数任意 t 返回常值。"""
        if name not in self._series:
            return float("nan")
        if self._is_param.get(name, False):
            return float(self._series[name][0])
        tt = self._time
        yy = self._series[name]
        if t < tt[0] or t > tt[-1]:
            return float("nan")
        # 二分找 t；精确命中重复时刻取右极限（最后一个相等的）
        lo, hi = 0, len(tt) - 1
        # 找第一个 >= t
        import bisect
        i = bisect.bisect_left(tt, t)
        if i < len(tt) and tt[i] == t:
            # 右极限：跳到最后一个等于 t 的
            while i + 1 < len(tt) and tt[i + 1] == tt[i]:
                i += 1
            return float(yy[i])
        if i == 0:
            return float(yy[0])
        x0, x1 = tt[i - 1], tt[i]
        y0, y1 = yy[i - 1], yy[i]
        if x1 == x0:
            return float(y0)
        return float(y0 + (y1 - y0) * (t - x0) / (x1 - x0))


def _decode_names(name_mat, n_signals):
    """从 MAT 的 char 矩阵解出 n_signals 个变量名。scipy 对 v4 char 矩阵的解码可能是
    maxLen 行（每行 n_signals 字符，列存）或 n_signals 行（每行 maxLen 字符）。"""
    arr = np.array(name_mat)
    if arr.ndim == 1:
        rows = [str(x) for x in arr]
    else:
        rows = ["".join(str(c) for c in row) for row in arr]
    if len(rows) == n_signals:
        # 每行就是一个完整名字
        return [r.replace("\x00", "").strip() for r in rows]
    # 列存：name[j] = 每行第 j 个字符拼起来
    names = []
    for j in range(n_signals):
        chars = [rows[r][j] for r in range(len(rows)) if j < len(rows[r])]
        names.append("".join(chars).replace("\x00", "").strip())
    return names


def read_mat(path):
    d = scipy.io.loadmat(path)
    data_info = np.array(d["dataInfo"])
    if data_info.shape[0] != 4:
        data_info = data_info.T
    n_signals = data_info.shape[1]
    names = _decode_names(d["name"], n_signals)

    data_1 = np.array(d["data_1"], dtype=float)
    data_2 = np.array(d["data_2"], dtype=float)
    # data_1 应为 nRows×2；data_2 应为 nSignals×nPoints（时间点数一般 > 变量数）
    if data_1.ndim == 2 and data_1.shape[1] != 2 and data_1.shape[0] == 2:
        data_1 = data_1.T
    # data_2：行=变量、列=时间点。若被转置（行数远大于列数且列数==已知变量数）则修正
    n_time_vars = int(np.sum(data_info[0, :] != 1))
    if data_2.shape[0] != 0:
        if data_2.shape[0] < data_2.shape[1]:
            pass  # 行少列多 = 变量×时间点，正常
        elif data_2.shape[1] == n_time_vars and data_2.shape[0] != n_time_vars:
            data_2 = data_2.T

    # 定位 time 行（data_2 第 0 行即 time）
    time = data_2[0] if data_2.shape[0] > 0 else np.array([0.0])

    series = {}
    is_param = {}
    for j, nm in enumerate(names):
        if not nm:
            continue
        dset = int(data_info[0, j])
        idx = int(data_info[1, j])
        sign = -1.0 if idx < 0 else 1.0
        ridx = abs(idx) - 1
        if dset == 1:
            val = sign * data_1[ridx, 0]
            series[nm] = np.full(len(time), val, dtype=float)
            is_param[nm] = True
        else:
            series[nm] = sign * data_2[ridx]
            is_param[nm] = False
    return Result(names, series, time, is_param)


def read_csv(path):
    with open(path, "r", newline="", encoding="utf-8", errors="replace") as f:
        reader = _csv.reader(f)
        header = next(reader)
        cols = [h.strip().strip('"') for h in header]
        data = [[float(x) for x in row] for row in reader if row]
    arr = np.array(data, dtype=float) if data else np.zeros((0, len(cols)))
    # 时间列：名为 time/Time，否则第一列
    tcol = 0
    for i, c in enumerate(cols):
        if c.lower() == "time":
            tcol = i
            break
    time = arr[:, tcol] if arr.size else np.array([0.0])
    series = {c: arr[:, i] for i, c in enumerate(cols)}
    is_param = {c: False for c in cols}
    return Result(cols, series, time, is_param)


def read(path):
    """按后缀读取；`.mat.xz` 先解压。"""
    p = str(path)
    if p.endswith(".mat.xz"):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, os.path.basename(p)[:-3])
            subprocess.run(["xz", "--keep", "--decompress", "--stdout", p],
                           stdout=open(out, "wb"), check=True)
            return read_mat(out)
    if p.endswith(".mat"):
        return read_mat(p)
    if p.endswith(".csv"):
        return read_csv(p)
    raise ValueError(f"Unknown result-file suffix: {p}")


def _selftest(mat_path):
    r = read(mat_path)
    print("vars:", r.variables())
    print("val(x,0.5)=", r.val("x", 0.5))
    print("val(y,1.0)=", r.val("y", 1.0))
    print("param k=", r.val("k", 0.0), "is_param:", r.is_param("k"))
    print("final x=", r.final("x"))


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        _selftest(sys.argv[1])
