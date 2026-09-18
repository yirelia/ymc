model V05_attr_var
  Real x = time;
  Real y(start = x);
equation
  der(y) = 1;
end V05_attr_var;
