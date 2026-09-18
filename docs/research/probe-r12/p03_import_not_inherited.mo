package P
  constant Real c = 1;
end P;
model Base
  import P.c;
  Real b = c;
end Base;
model P03
  extends Base;
  Real a = c;
end P03;
