// 同一基类两次，外层实例修饰同时打到两份上（lookupElementsPtr 返回多个）→ 仍相同
model A
  parameter Real p = 1;
end A;
model B
  extends A;
  extends A;
end B;
model P36_DupModOneSide
  B b(p = 5);
end P36_DupModOneSide;
