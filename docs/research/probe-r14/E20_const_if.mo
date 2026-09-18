model E20_const_if
  constant Boolean useA = false;
  constant Real a = 1;
  parameter Real b = 2;
  Real x = if useA then a else b;
  Real y = if useA then 1 / 0 else b;
equation
  if useA then
    assert(false, "never");
  end if;
  assert(b > 0, "b must be positive");
  assert(a > 0, "a must be positive");
end E20_const_if;
