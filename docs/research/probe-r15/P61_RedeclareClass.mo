model A
  replaceable model B
    Real x;
  end B;
  B b;
end A;

model C
  Real x = 1.0;
  Real z = 5.0;
end C;

model P61_RedeclareClass
  A a(redeclare model B = C);
end P61_RedeclareClass;
