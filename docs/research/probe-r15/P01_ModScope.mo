// 修饰里的表达式在"写修饰的那个类"的作用域里解析
model A
  parameter Real p = 1;
  parameter Real q = 10;
  Real x = p;
end A;

model B
  parameter Real q = 2;
  A a(p = q);        // q -> B.q (不是 A.q)
end B;

model P01_ModScope
  parameter Real q = 3;
  B b(q = q + 1, a(q = q * 100));    // 两个 q 都是 P01_ModScope.q
end P01_ModScope;
