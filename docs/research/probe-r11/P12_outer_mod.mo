model Leaf
  outer Real T0 = 5;
end Leaf;

model P12
  inner Real T0 = 300;
  Leaf leaf;
end P12;
