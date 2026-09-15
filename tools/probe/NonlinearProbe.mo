// name: NonlinearProbe
// keywords: probe, nonlinear
// status: correct
// 非线性代数环探针：y、z 通过 sin 与二次式互相耦合 -> 一个非线性强连通分量。
// 用于验证 S5 撕裂/非线性块判定、S6 非线性求解。
model NonlinearProbe
  Real x(start = 0.5);
  Real y;
  Real z;
equation
  der(x) = -x + z;
  y = sin(z) + x;
  z = y * y - 3.0;
end NonlinearProbe;
