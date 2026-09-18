model A
  record R
    Real r = 1;
  end R;
  Real v;
end A;
model P33
  A a;
  A.R rec = a.R(r = 2);
end P33;
