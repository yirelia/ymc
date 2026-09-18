// each 用在非数组上：NF 是否警告？
model A
  parameter Real p = 0;
end A;

model P15_EachOnScalar
  A a(each p = 1);
  Real x(each start = 2);
end P15_EachOnScalar;
