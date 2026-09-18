package Lib
  constant Real k = 7;
  model A
    Real x = k;
  end A;
end Lib;
model P26
  constant Real k = 100;
  Lib.A a;
end P26;
