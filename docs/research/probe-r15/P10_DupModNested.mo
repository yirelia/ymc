// a(x(start=1), x(fixed=true)) 合法；a(x(start=1), x(start=2)) 非法
model A
  Real x;
end A;

model P10_DupModNested
  A a1(x(start = 1), x(fixed = true)) ;
  A a2(x(start = 1), x(start = 2));
end P10_DupModNested;
