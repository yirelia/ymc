// 被删组件上的修饰：修饰里的错误是否还报？（绑定类型错 → INVALID_BINDING 延迟到压平，组件被删则不报）
model A
  parameter Real p = 1;
end A;
model P51_CondMod
  parameter Boolean on = false;
  A a(p = "str") if on;
  Real y = true if on;
end P51_CondMod;
