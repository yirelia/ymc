// 菱形继承：同一基类进来两次 → 重复元素相同 → 只保留一个
model A
  parameter Real p = 1;
  Real x = p;
end A;
model B1 extends A; end B1;
model B2 extends A; end B2;
model P31_DupIdentical
  extends B1;
  extends B2;
  extends A;
end P31_DupIdentical;
