model Leaf
  outer model M = MBase;
  M m;
end Leaf;

model MBase
  Real q = 1;
end MBase;

model MImpl
  extends MBase;
  Real extra = 2;
end MImpl;

model P16
  inner model M = MImpl;
  Leaf leaf;
end P16;
