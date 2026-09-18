model A
protected
  parameter Real p = 1;
end A;
model B
  extends A(p = 2);     // extends 里可以改 protected
end B;
model P24_ModifyProtected
  B b;
  A a(p = 3);           // 组件修饰不能改 protected
end P24_ModifyProtected;
