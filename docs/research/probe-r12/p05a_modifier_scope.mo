model A
  parameter Real p = 1;
  parameter Real q = 2;
end A;
model P05a
  parameter Real q = 10;
  A a(p = q);
end P05a;
