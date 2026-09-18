// 条件组件：false → 变量与涉及它的 connect 一起消失；true → 保留
connector C
  Real e;
  flow Real f;
end C;
model P50_CondBasic
  parameter Boolean on = false;
  parameter Boolean on2 = true;
  C c1 if on;
  C c2 if on2;
  C c3;
  Real x(start = 1) if on;
equation
  connect(c1, c3);
  connect(c2, c3);
end P50_CondBasic;
