// 短类链：每一层 DERIVED 的修饰与外层 merge 后下传给 baseClass
model A
  parameter Real p = 1;
  parameter Real q = 1;
  parameter Real r = 1;
end A;
model S1 = A(p = 2, q = 2);
model S2 = S1(p = 3);

model P21_ShortChain
  S2 s(r = 4);
  S2 t[2](each q = 5, r = {6, 7});
end P21_ShortChain;
