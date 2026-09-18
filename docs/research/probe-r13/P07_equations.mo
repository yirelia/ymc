connector Pin
  Real v;
  flow Real i;
end Pin;

model Res
  Pin p, n;
  parameter Real R = 1;
equation
  p.v - n.v = R * p.i;
  p.i + n.i = 0;
end Res;

model P07_equations
  Res r1, r2;
  Real x(start = 1), y, z;
  discrete Real d1, d2;
  parameter Boolean flag = true;
  parameter Boolean pb(fixed = false);
  parameter Integer n = 3;
  Real arr[n];
  Boolean cond;
initial equation
  pb = true;
  if pb then
    y = 1;
  else
    y = 2;
  end if;
equation
  connect(r1.n, r2.p);
  connect(r1.p, r2.n);
  r1.p.v = 0;
  der(x) = -x "equation comment";
  cond = x > 0.5;
  if cond then
    z = 1;
  elseif x > 0.2 then
    z = 2;
    assert(z > 0, "inner assert");
  elseif x > 0.1 then
    z = 3;
  else
    z = 4;
  end if;
  if flag then
    y = x;
  else
    y = -x;
  end if;
  for i in 1:n loop
    arr[i] = i * x;
  end for;
  when x < 0.5 then
    d1 = pre(d1) + 1;
    reinit(x, 1.0);
  elsewhen x < 0.3 then
    d1 = 0;
  elsewhen {x < 0.2, x < 0.1} then
    d1 = -1;
  end when;
  when initial() then
    d2 = 1;
  end when;
  assert(x > -1, "x too small");
  assert(x < 10, "x large: " + String(x), AssertionLevel.warning);
  assert(x < 20, "x larger", level = AssertionLevel.error);
  when x < 0.05 then
    terminate("done");
  end when;
end P07_equations;
