model Root
  Real r = 1;
  Real q = 2;
end Root;

model L
  Real l0 = 3;
  extends Root;
  Real l1 = 4;
end L;

model R
  extends Root;
  Real r1 = 5;
end R;

model P04
  Real first = 0;
  extends L;
  Real mid = 6;
  extends R;
  Real q = 2;
  Real last = 7;
end P04;
