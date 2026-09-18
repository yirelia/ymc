model T01_int_to_real
  Integer i = 3;
  parameter Integer n = 2;
  constant Integer k = 5;
  Real a = 1;
  Real b = i;
  Real c = n;
  Real d = k;
  Real e = i + 1;
  Real f = i * 2.5;
  Real g = i / 2;
  Real h = 7 / 2;
  Real m = n ^ 2;
  Real o = -i;
  Real p = if a > 0 then i else n;
  Real q[2] = {1, 2};
  Real r[2] = {i, 2.0};
  Real s = abs(i);
  Real t = sin(i);
  Real u = max(i, n);
equation
end T01_int_to_real;
