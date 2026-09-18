model P14_small
  type E = enumeration(a, b);
  String s(start = "abc", fixed = true);
  Real xe[E] = {1, 2};
  Real xb[Boolean] = {3, 4};
  Integer k(start = -1);
  Integer j = k * (-1) + (-3) - (-k);
  Real r = 2 * (-3.5) + k;
  Real t = xe[E.a] + xb[true];
  parameter Real pneg = -1;
  parameter Integer ineg = -2;
  Real u = -pneg + ineg;
equation
  when time > 1 then
    s = "x";
    k = pre(k) - 1;
  end when;
end P14_small;
