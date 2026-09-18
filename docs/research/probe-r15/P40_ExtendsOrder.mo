// 继承元素在压平结果中的位置 = extends 子句所在位置
model A
  Real a1;
  Real a2;
end A;
model P40_ExtendsOrder
  Real x;
  extends A;
  Real y;
end P40_ExtendsOrder;
