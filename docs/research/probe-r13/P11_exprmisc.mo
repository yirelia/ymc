model P11_exprmisc
  type E = enumeration(a, b, c);
  function fn
    input Real x;
    input Real k = 2.0;
    input String s = "d";
    output Real y;
  algorithm
    y := k * x;
  end fn;
  function fv
    input Real v[:];
    output Real y;
  algorithm
    y := sum(v);
  end fv;
  parameter Integer n = 3;
  parameter Integer np(fixed = false);
  Integer i1, i2;
  Real x(start = 1), r1, r2, r3, r4, r5, r6, r7, r8, r9;
  Real v[3], w[3], m[2, 2];
  E e1, e2;
  Boolean b1, b2, b3, b4;
  String s1, s2, s3;
  Real q[12];
  parameter Real pv[3] = {1, 2, 3};
  parameter Real pm[2, 2] = {{1, 2}, {3, 4}};
  parameter Real pfill[2] = fill(1.5, 2);
  parameter Real pz[2] = zeros(2);
  parameter Real plin[3] = linspace(0, 1, 3);
  parameter Real prange[3] = 1.0:3.0;
  parameter Integer pcat[4] = cat(1, {1, 2}, {3, 4});
initial equation
  np = 2;
equation
  der(x) = -x;
  i1 = integer(x) + np;
  i2 = if x > 0.5 then n else np;
  r1 = i1 + x;
  r2 = i1 / i2;
  r3 = i1 * 2 + 1;
  r4 = fn(x);
  r5 = fn(x, s = "lit", k = 3);
  r6 = fn(k = i1, x = i2);
  r7 = fv(v[2:3]) + fv(v[{1, 3}]) + fv(v) + v[i1];
  r8 = fv({x, 2 * x, i1});
  r9 = sum(v[i] * w[i] for i in 1:3) + sum(i for i in 1:np) + max(v) + min(x, 2) + product(v);
  v = {x, 2 * x, 3 * x};
  w = v * 2 + cross(v, v) .* pv;
  m = pm * x + transpose(pm) - identity(2);
  e1 = if x > 0.5 then E.a else E.c;
  e2 = E.b;
  b1 = e1 == E.a and e2 < E.c;
  b2 = pre(b1) or edge(b1) or change(b1) or initial() or terminal();
  b3 = sample(0, 0.1);
  b4 = b1 <> b2;
  s1 = "a\"b\c\nd\te?%'" + String(i1) + String(x, significantDigits = 3) + String(b1) + String(e1);
  s2 = String(x, format = "%g");
  s3 = if b1 then "yes" else "no";
  q[1] = noEvent(abs(x)) + smooth(1, x) + homotopy(x, 1);
  q[2] = div(i1, 2) + mod(i1, 2) + rem(i1, 2) + ceil(x) + floor(x) + sign(x) + sqrt(x);
  q[3] = sin(x) + cos(x) + tan(x) + asin(x) + acos(x) + atan(x) + atan2(x, 1) + sinh(x) + cosh(x) + tanh(x) + exp(x) + log(x) + log10(x);
  q[4] = size(v, 1) + size(m, 2) + ndims(m) + Integer(e1);
  q[5] = delay(x, 0.1) + semiLinear(x, 1, 2) + der(x) * 2;
  q[6] = v * w + scalar({{x}}) + max(x, i1) + m[1, 2] + m[i1, i2];
  q[7] = x ^ 2 + x ^ 0.5 + x ^ n + 2 ^ n + i1 ^ 2;
  q[8] = time + (if b1 then 1 else 0) + abs(i1) + max(i1, i2);
  q[9] = Modelica.Constants.pi + Modelica.Constants.eps;
  q[10] = (1 + i1) * x;
  q[11] = -i1;
  q[12] = i1 - i2 * (3 - i1);
  annotation(uses(Modelica(version = "4.0.0")));
end P11_exprmisc;
