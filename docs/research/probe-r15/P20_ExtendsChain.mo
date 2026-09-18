// 多层 extends：越外层（越靠近派生类）的 extends 修饰优先
model A
  parameter Real p = 1;
  parameter Real q = 1;
  parameter Real r = 1;
end A;

model B
  extends A(p = 2, q = 2);
end B;

model C
  extends B(p = 3);
end C;

model P20_ExtendsChain
  extends C;
  C c(r = 9);
end P20_ExtendsChain;
