model E03_struct_dim
  parameter Integer n = 2;
  parameter Integer m = n + 1;
  parameter Integer k = 4;
  parameter Real w = 2.0;
  Real x[n];
  Real y[m];
  Real s;
equation
  x = fill(w, n);
  for i in 1:m loop
    y[i] = i * k + n;
  end for;
  s = sum(x) + n + m + k;
end E03_struct_dim;
