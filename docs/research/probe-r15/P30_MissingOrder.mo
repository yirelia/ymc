// 子修饰在 AVL 树里按名字排序：两个都不存在时先报字典序小的
model A
  Real x;
end A;

model P30_MissingOrder
  A a(zz = 1, bb = 2);
end P30_MissingOrder;
