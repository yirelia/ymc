// 传播路径 (a)-(e) 合在一个模型里
model N
  parameter Real p = 1;
  Real x(start = p);
end N;

model M = N(p = 4);             // (c) 短类定义

model B
  extends N(p = 3);             // (b) extends 修饰
end B;

model P19_PathAll
  N n(p = 2);                   // (a) 组件声明上的修饰
  B b;
  M m;
  M m2(p = 5);                  // 组件修饰 > 短类修饰
  B b2(p = 6, x(start = 7));    // 组件修饰 > extends 修饰；(e) 内建属性
end P19_PathAll;
