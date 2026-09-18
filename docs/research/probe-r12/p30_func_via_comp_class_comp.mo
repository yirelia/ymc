model C
  function f
    output Real y = 1.0;
  end f;
end C;
model B
  C c;
end B;
model A
  model BB = B;
  BB inst;
end A;
model P30
  A a;
  Real x = a.BB.c.f();
end P30;
