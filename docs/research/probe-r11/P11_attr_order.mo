type T = Real(start = 1, max = 5);
model P11
  T x(unit = "m", nominal = 2, min = -1, quantity = "Q");
  Real y(fixed = true, start = 0, displayUnit = "mm", unit = "m");
equation
  der(x) = 1;
  y = x;
end P11;
