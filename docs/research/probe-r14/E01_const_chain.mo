package E01P
  constant Real g = 9.81;
  constant Integer N = 3;
end E01P;
model E01_const_chain
  constant Real PI = 3.14;
  constant Real TWO_PI = 2 * PI;
  constant Real c3 = TWO_PI + E01P.g;
  constant Integer n = E01P.N + 1;
  parameter Real p = PI * 2;
  parameter Real p2 = E01P.g;
  Real x;
  Real y;
  Real z(start = PI);
equation
  x = 2.0 * PI;
  y = TWO_PI * x + E01P.g + n;
  z = c3 * time;
end E01_const_chain;
