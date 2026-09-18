model A
  Real a1 = 1;
  Real a2 = 2;
end A;

model B
  Real b1 = 3;
  extends A;
  Real b2 = 4;
end B;

model C
  Real c1 = 5;
  extends B;
  Real c2 = 6;
protected
  Real c3 = 7;
  extends A2;
  Real c4 = 8;
end C;

model A2
  Real z1 = 9;
protected
  Real z2 = 10;
public
  Real z3 = 11;
end A2;

model P02
  extends C;
  Real top = 0;
end P02;
