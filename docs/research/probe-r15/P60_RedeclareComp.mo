model AA
  Real x = 1.0;
end AA;

model BB
  extends AA;
  Real y = 2.0;
end BB;

model Wrap
  replaceable AA c;
end Wrap;

model P60_RedeclareComp
  Wrap w(redeclare BB c);
end P60_RedeclareComp;
