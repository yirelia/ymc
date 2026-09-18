// extends 修饰的作用域 = 派生类（含继承来的名字）
model A
  parameter Real p = 1;
  parameter Real k = 7;
  Real x = p;
end A;

model P02_ModScopeExtends
  extends A(p = k + r);   // k 是继承来的 A.k，r 是本地的
  parameter Real r = 5;
end P02_ModScopeExtends;
