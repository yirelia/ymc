// 本地元素与继承元素同名且相同 / 不同
model A
  Real x = 1;
  Real y;
end A;
model P33_DupLocalInherited
  extends A;
  Real x = 1;      // 相同 -> 合法（保留哪个？看位置）
  Real z;
end P33_DupLocalInherited;
