model A
  replaceable constant Real x;
end A;

model P68_RedeclareVariability
  A a(redeclare parameter Real x);
end P68_RedeclareVariability;
