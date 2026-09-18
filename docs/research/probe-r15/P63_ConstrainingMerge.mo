model A
  replaceable Real x constrainedby Real(start = 1.0);
end A;

model P63_ConstrainingMerge
  A a(redeclare Real x(min = 1.0));
end P63_ConstrainingMerge;
