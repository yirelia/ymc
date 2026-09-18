model V14_const_array
  constant Real c[3] = {1, 2, 3};
  constant Integer idx = 2;
  parameter Integer pi = 1;
  Integer vi = 3;
  Real x = c[idx];
  Real y = c[pi];
  Real z = c[vi];
  Real s = sum(c);
  Real w = c[1] + c[2] * x;
end V14_const_array;
