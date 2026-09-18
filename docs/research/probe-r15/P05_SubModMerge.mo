// 同名子修饰递归合并：外层只改 start，内层的 fixed/min 保留；绑定与属性独立
model A
  Real x(start = 1, fixed = true, min = -1) = 0;
end A;

model B
  A a(x(start = 2));
end B;

model P05_SubModMerge
  B b(a(x(min = -5) = 3));
end P05_SubModMerge;
