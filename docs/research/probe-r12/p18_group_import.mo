package P
  constant Real a = 1;
  constant Real b = 2;
end P;
model P18
  import P.{a, bb = b};
  Real x = a + bb;
end P18;
