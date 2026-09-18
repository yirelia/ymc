package E17P
  constant Real tbl[3] = {10, 20, 30};
  function f
    input Real u;
    output Real y;
  algorithm
    y := 2 * u + 1;
  end f;
end E17P;
model E17_func_eval
  constant Real c = E17P.f(2);
  parameter Real p = E17P.f(3);
  parameter Real q = 1;
  parameter Real r = E17P.f(q);
  Integer vi = 2;
  Real x = E17P.f(4);
  Real y = E17P.f(x);
  Real z = E17P.tbl[vi];
  Real w = E17P.tbl[2];
end E17_func_eval;
