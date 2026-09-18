model E08_unbound_param
  parameter Real q(start = 2.5);
  parameter Integer n(start = 3);
  parameter Real r(fixed = false);
  Real x;
initial equation
  r = 1;
equation
  x = q + n + r;
end E08_unbound_param;
