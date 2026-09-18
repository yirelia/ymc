model A
  function f
    output Real y = 1.0;
  end f;
end A;
model P34
  A a;
  Real x = a.f;
end P34;
