model V09_record_param
  record R
    Real a = 1;
    Integer n = 2;
  end R;
  parameter R r;
  constant R cr(a = 3);
  R vr;
  Real x = r.a + cr.a + cr.n;
equation
  vr.a = time;
  vr.n = 1;
end V09_record_param;
