record C
  function f
    output Real y = 1.0;
  end f;
  Real r = 0;
end C;
model B
  constant C c;
end B;
model A
  model BB = B;
  BB inst;
end A;
model P30b
  A a;
  Real x = a.BB.c.f();
end P30b;
