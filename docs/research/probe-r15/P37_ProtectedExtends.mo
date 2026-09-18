// extends 出现在 protected 段：继承元素全部变 protected
model A
  parameter Real p = 1;
  Real x = p;
end A;
model B
protected
  extends A;
public
  Real y = x;
end B;
model P37_ProtectedExtends
  B b;
end P37_ProtectedExtends;
