// 数组组件修饰的切分：非 each 的绑定按元素切；each 原样给每个元素
model A
  parameter Real p = 0;
  parameter Real q = 0;
  Real x(start = 0);
end A;

model P12_ArraySplit
  parameter Real v[3] = {10, 20, 30};
  A a[3](p = {1, 2, 3}, each q = 5, x(start = v));
end P12_ArraySplit;
