model Sub
  Real s;
equation
  s = 100;
end Sub;

model Base1
  Real x1;
equation
  x1 = 1;
end Base1;

model Base2
  Real x2;
equation
  x2 = 2;
end Base2;

model P03
  Real a;
  extends Base1;
  Sub sub;
  extends Base2;
  Real b;
equation
  a = 10;
  b = 20;
end P03;
