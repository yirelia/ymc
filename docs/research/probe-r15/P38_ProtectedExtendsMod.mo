model A
  parameter Real p = 1;
end A;
model B
protected
  extends A;
end B;
model P38_ProtectedExtendsMod
  B b(p = 2);
end P38_ProtectedExtendsMod;
