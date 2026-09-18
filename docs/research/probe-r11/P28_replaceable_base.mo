model Base
  Real q = 1;
end Base;

model P28
  replaceable model M = Base;
  extends M;
end P28;
