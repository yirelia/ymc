package Pkg
  type E = enumeration(one, two);
  record R
    Real x;
    Integer n = 1;
    E e = E.one;
  end R;
  record Nested
    R r;
    Real z[2];
  end Nested;
end Pkg;

model M
  Real x "inner comment";
  parameter Integer n = 2;
  Real a[n] "array comment";
end M;

model P04_types
  Pkg.R r "record comment";
  Pkg.Nested nn;
  Pkg.R ra[2];
  M m "component comment";
  M ma[2];
  Real mat[2, 3] "matrix";
  Pkg.E ee(start = Pkg.E.two) "enum var";
  Real x = 1.0 "bound" annotation(Dialog(group = "g"));
  Real y annotation(HideResult = true);
  parameter Real p[2] = {1, 2};
  parameter Real q[2, 2] = {{1, 2}, {3, 4}};
  Real e0[0];
  Boolean bb[2](each start = true);
  Real st[3](start = {1, 2, 3}, each fixed = true);
equation
  r.x = 1; nn.r.x = 2; nn.z = {1, 2};
  ra[1].x = 1; ra[2].x = 2;
  m.x = 1; m.a = {1, 2};
  ma[1].x = 1; ma[2].x = 1; ma[1].a = {1, 2}; ma[2].a = {3, 4};
  mat = {{1, 2, 3}, {4, 5, 6}};
  ee = Pkg.E.one;
  y = 2;
  bb = {true, false};
  st = {1, 1, 1};
end P04_types;
