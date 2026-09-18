model A
  parameter Real p = 1;
  parameter Real zz = 2;
end A;
model P05b
  A a(p = zz);
end P05b;
