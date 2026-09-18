// 在方程里引用被删组件
model P54_CondUse
  parameter Boolean on = false;
  Real x if on;
  Real y;
equation
  y = x;
end P54_CondUse;
