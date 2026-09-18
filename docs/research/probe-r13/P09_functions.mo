package P09
  function zeta "zeta comment"
    input Real x "in x";
    input Integer n = 2 "default";
    input Real v[:] "vector";
    input Real m[:, 2];
    output Real y "out y";
    output Real w[size(v, 1)];
  protected
    Real tmp(start = 1.0) = 2 * x "local";
    constant Real k = 3;
    parameter Integer q = 1;
    Real loc[n];
  algorithm
    tmp := tmp + x ^ n;
    y := tmp * sum(v) + k + m[1, 2];
    w := v * 2;
    for i in 1:size(v, 1) loop
      w[i] := w[i] + loc[1];
    end for;
    annotation(Inline = false, Documentation(info = "x"));
  end zeta;

  function alpha
    input Real x;
    output Real y;
  external "C" y = sin(x) annotation(Include = "#include <math.h>");
  end alpha;

  function ext2
    input Real x[:];
    output Real y;
  external "C" y = my_sum(x, size(x, 1)) annotation(Library = "mylib");
  end ext2;

  function ext3
    input Real x;
    output Real y;
  external "C";
  end ext3;

  impure function imp
    input Real x;
    output Real y;
  algorithm
    y := x;
  end imp;

  function noalg
    input Real x;
    output Real y = x + 1;
  end noalg;

  model M
    Real a = zeta(time, v = {1, 2}, m = {{1, 2}});
    Real b = alpha(time);
    Real c = zeta(time, 3, {1.0}, {{1, 2}, {3, 4}});
    Real d = ext2({time, 2});
    Real e = ext3(time);
    Real f = imp(time);
    Real g = noalg(time);
    Real w2[2];
  equation
    (, w2) = zeta(time, 3, {1.0, 2.0}, {{1, 2}});
  end M;
end P09;
