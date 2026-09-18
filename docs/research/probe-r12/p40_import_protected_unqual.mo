package P
  constant Real a = 1;
protected
  constant Real c = 2;
end P;
model P40
  import P.*;
  Real x = a + c;
end P40;
