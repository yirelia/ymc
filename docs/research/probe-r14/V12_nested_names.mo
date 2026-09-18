model V12_nested_names
  model A
    parameter Real q(start = 2.5);
    parameter Integer n = 2;
    Real x[n] = fill(q, n);
  end A;
  A a;
  A b(n = 3);
end V12_nested_names;
