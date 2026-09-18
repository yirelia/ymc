package Q
  constant Real c = 2;
end Q;
model P02c
  import Q.c;
  constant Real c = 4;
  Real a = c;
end P02c;
