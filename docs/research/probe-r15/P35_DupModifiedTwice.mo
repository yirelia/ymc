// 同一基类两次、修饰不同 → 绑定不同 → 不相同
model A
  parameter Real p = 1;
end A;
model P35_DupModifiedTwice
  extends A(p = 2);
  extends A(p = 3);
end P35_DupModifiedTwice;
