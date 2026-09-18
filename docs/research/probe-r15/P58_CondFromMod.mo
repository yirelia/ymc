// 条件取决于外层修饰过来的参数；数组实例里每个元素各自判定
model A
  parameter Boolean on = true;
  Real x = 1 if on;
  Real y = 2;
end A;
model P58_CondFromMod
  A a1(on = false);
  A a2;
  A a3[2](on = {true, false});
end P58_CondFromMod;
