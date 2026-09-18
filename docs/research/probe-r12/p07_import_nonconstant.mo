model Q
  Real x = 1;
end Q;
model P07
  import Q.x;
  Real y = x;
end P07;
