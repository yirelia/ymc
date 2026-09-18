// 对嵌套类（非组件）的修饰：a(T(start = 5)) —— 落到 Class.modifier 上
model A
  type T = Real(start = 1);
  T x;
  model In
    parameter Real k = 1;
  end In;
  In i;
end A;

model P22_ClassMod
  A a(T(start = 5), In(k = 8));
end P22_ClassMod;
