function sin
  input Real x;
  output Real y;
algorithm
  y := 2 * x;
end sin;
model P44
  Real y = sin(1.0);
end P44;
