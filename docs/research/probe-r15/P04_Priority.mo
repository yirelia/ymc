// 优先级：外层实例修饰 > extends 修饰 > 声明处默认值；多层
model A
  parameter Real p = 1;
  parameter Real q = 1;
  parameter Real r = 1;
  parameter Real s = 1;
end A;

model B
  extends A(p = 2, q = 2, r = 2);
end B;

model C
  B b(p = 3, q = 3);
end C;

model P04_Priority
  C c(b(p = 4));
end P04_Priority;
