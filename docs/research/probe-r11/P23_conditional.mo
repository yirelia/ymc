model Sub
  Real s = 1;
end Sub;

model P23
  parameter Boolean use = false;
  parameter Boolean use2 = true;
  Sub a if use;
  Real mid = 0;
  Sub b if use2;
  Real r if not use2;
end P23;
