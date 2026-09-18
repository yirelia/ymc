model A
  Real x(start = 1);
  parameter Real p = 1;
end A;

model P09_DupMod
  A a(p = 1, p = 2);
end P09_DupMod;
