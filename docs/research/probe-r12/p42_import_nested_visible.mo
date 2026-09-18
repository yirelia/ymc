package P
  constant Real c = 2;
end P;
model P42
  import P.c;
  model Inner
    Real x = c;
  end Inner;
  Inner i;
end P42;
