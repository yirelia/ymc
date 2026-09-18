model T03_discrete_var
  Integer i;
  Boolean b;
  String s = "x";
  discrete Real dr;
  Real x;
  Real y;
equation
  x = time;
  i = integer(x);
  b = x > 0.5;
  when x > 1 then
    dr = x;
    y = 2 * x;
  end when;
end T03_discrete_var;
