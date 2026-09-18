model P01c
  function sin
    input Real x;
    output Real y;
  algorithm
    y := 2 * x;
  end sin;
  Real y = sin(1.0);
end P01c;
