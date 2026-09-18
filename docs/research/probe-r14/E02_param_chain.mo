model E02_param_chain
  parameter Real q = 1.5;
  parameter Real p = 2 * q;
  parameter Real r = p + q + 1;
  parameter Integer n = 2;
  parameter Integer m = n * 3;
  Real x;
  Real y;
equation
  x = p * time + 2 * 3;
  y = r * m + n;
end E02_param_chain;
