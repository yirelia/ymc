model P13_exprmisc2
  function rsum
    input Real v[:];
    output Real y;
  algorithm
    y := sum(v);
  end rsum;
  function g
    input Integer iv[:];
    input Integer n0;
    input Integer n1;
    input Real a[:];
    input Real b[:];
    output Real y;
  protected
    Real t[size(a, 1)];
    Integer k;
  algorithm
    y := rsum(iv);
    for i in n0 + 1:n1 - 1 loop
      y := y + i;
    end for;
    for i in n1:-1:n0 loop
      y := y + i;
    end for;
    for i in 1:2 * n0:n1 loop
      y := y + i;
    end for;
    t := (a + b) * 2 - a .* b + a / 2;
    y := y + a * b + t[end] + a[end - 1];
    k := if n0 > 1 then n0 else -n0;
    y := y + (if k > 0 then 1 else 2) * 3;
    y := y + rsum(if k > 0 then a else b);
    y := y + rsum({a[i] ^ 2 for i in 1:size(a, 1)});
    y := y + rsum(a[2:end]) + rsum(a[:]) ;
    k := -k;
    k := -(k + 1);
    k := k * (-1);
    y := -y;
    y := -(y + 1) * 2;
    y := -(y * 2) + 1;
    y := (-y) * 2;
    y := 2 * (-y);
    y := 2 - (-y);
    y := -y ^ 2;
    y := (-y) ^ 2;
    y := y ^ (-2);
    y := (y + 1) ^ 2;
    y := (y * 2) ^ 2;
    y := 2 ^ (y ^ 1) + 1;
  end g;
  function mat
    output Real m[2];
  algorithm
    m := {1, 2};
  end mat;
  Real y = g({1, 2}, 1, 3, {time, 1}, {2, time});
  parameter Real pf[2] = mat();
  Real z[2] = mat() * time;
  String s = "tab	raw, nl:
second line, bs:\ q:\" bell:\a end";
equation
end P13_exprmisc2;
