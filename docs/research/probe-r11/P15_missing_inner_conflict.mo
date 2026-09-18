model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model P15
  Real T0 = 1;
  Leaf leaf;
end P15;
