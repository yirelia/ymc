// name: EventProbe
// keywords: probe, event, hybrid
// status: correct
// 混合系统探针：过零事件 + pre/edge。用于验证 S5 事件处理编译侧、S6/S7 事件定位。
model EventProbe
  Real x(start = 1.0);
  Boolean high(start = false);
equation
  der(x) = -x + 0.5;
  high = x > 0.6;
end EventProbe;
