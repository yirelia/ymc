model E16_struct_dep_chain
  parameter Integer a = 2;
  parameter Integer b = a + 1;
  parameter Integer c = b * 2;
  parameter Real w = c;
  parameter Real v = a * 1.5;
  Real x[c];
  Real y = a + b + c;
equation
  x = fill(v, c);
end E16_struct_dep_chain;
