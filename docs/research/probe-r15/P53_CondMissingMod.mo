// 被删组件上修饰了不存在的元素：实例化阶段就报（与条件无关）
model A
  parameter Real p = 1;
end A;
model P53_CondMissingMod
  parameter Boolean on = false;
  A a(q = 2) if on;
end P53_CondMissingMod;
