model Sub
  replaceable model M = Base;
  M m;
end Sub;

model Base
  Real q = 1;
end Base;

model P27
  Sub a(M = Base);
end P27;
