// x = 1 与 x(start = 2) 分两处写：合法（一处有绑定一处没有）
model A
  Real x;
end A;

model P11_DupModBindingAndSub
  A a(x = 1, x(start = 2));
end P11_DupModBindingAndSub;
