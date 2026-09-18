model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model P08
  Real before = 0;
  Leaf leaf;
  Real after = 1;
end P08;
