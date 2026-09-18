model Leaf
  outer Real X;
  Real t = X;
end Leaf;

model P13
  inner model X
    Real q = 1;
  end X;
  Leaf leaf;
end P13;
