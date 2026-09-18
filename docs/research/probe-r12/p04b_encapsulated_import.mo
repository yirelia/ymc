package Top
  constant Real k = 5;
end Top;
encapsulated model P04b
  import Top;
  Real x = Top.k + sin(time);
  Integer n = integer(2.5);
end P04b;
