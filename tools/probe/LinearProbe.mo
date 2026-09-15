// name: LinearProbe
// keywords: probe, linear
// status: correct
// 最小线性 ODA 探针：1 个状态 + 1 个代数变量 + 1 个参数。
// 用于验证 S3 压平、S5 因果化（单强连通分量）、S6 数值。
model LinearProbe
  Real x(start = 1.0);
  Real y;
  parameter Real k = 2.0;
equation
  der(x) = -k * x + 1.0;
  y = x * x + k;
end LinearProbe;
