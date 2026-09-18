model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model Mid
  inner outer Real T0 = 2 * 1;
  Leaf leaf;
end Mid;

model P09
  inner Real T0 = 300;
  Mid mid;
  Leaf leaf;
end P09;
