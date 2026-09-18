model Root1
  Real r = 1;
end Root1;

model Root2
  Real r = 2;
end Root2;

model P05
  extends Root1;
  extends Root2;
end P05;
