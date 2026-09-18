model A
  Real x = 1.0;
end A;

model B
  extends A;
  Real y = 3.0;
end B;

model Wrap
  A a;
end Wrap;

model P62_RedeclareNonReplaceable
  extends Wrap(redeclare B a);
end P62_RedeclareNonReplaceable;
