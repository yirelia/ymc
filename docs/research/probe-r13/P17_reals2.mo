model P17_reals2
  function fv
    input Real v[3];
    output Real y;
  algorithm
    y := sum(v);
  end fv;
  constant Real c1 = 10000.0;
  constant Real c2 = 12000.0;
  constant Real c3 = 120000.0;
  constant Real c4 = 1234.5;
  constant Real c5 = 0.00012;
  constant Real c6 = 123000.0;
  constant Real c7 = 1230000.0;
  constant Real c8 = 100.0;
  constant Real c9 = 0.01;
  Integer iv[3];
  Real y = fv(iv);
equation
  iv = {integer(time), 2, 3};
end P17_reals2;
