// 两层数组：b[2].a[3].p，外层给 2x3 矩阵；内层声明处修饰只切一层
model A
  parameter Real p = 0;
  parameter Real q = 0;
end A;

model B
  A a[3](q = {7, 8, 9});
end B;

model P13_ArraySplit2D
  B b[2](a(p = {{1, 2, 3}, {4, 5, 6}}));
end P13_ArraySplit2D;
