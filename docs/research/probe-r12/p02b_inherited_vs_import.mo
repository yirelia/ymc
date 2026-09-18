package Q
  constant Real c = 2;
end Q;
model Base
  constant Real c = 3;
end Base;
model P02b
  extends Base;
  import Q.c;
  Real a = c;
end P02b;
