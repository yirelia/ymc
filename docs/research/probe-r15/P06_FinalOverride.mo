model A
  parameter Real p = 1;
end A;

model B
  A a(final p = 2);
end B;

model P06_FinalOverride
  B b(a(p = 3));
end P06_FinalOverride;
