model Leaf
  outer Real T0;
  Real t = T0 + 1;
end Leaf;

model Mid
  Leaf leaf;
  Real m = 2;
end Mid;

model P07
  Mid mid;
  inner Real T0 = 300;
  Leaf leaf2;
end P07;
