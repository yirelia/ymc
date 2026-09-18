model E10b_unbound_const_eq
  constant Real a;
  constant Real b = a + 1;
  Real x;
equation
  x = a * 2;
end E10b_unbound_const_eq;
