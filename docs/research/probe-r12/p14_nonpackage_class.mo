model A
  model B
    Real x;
  end B;
  constant Real c = 1;
  Real v = 2;
end A;
model P14
  A.B b;
  Real y = A.c;
end P14;
