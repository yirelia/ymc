model P08_statements
  function f2
    input Real x;
    output Real a;
    output Real b;
  algorithm
    a := x;
    b := 2 * x;
  end f2;
  Real x(start = 1), y, z, a, b, s;
  Integer k;
  discrete Real d;
  Real arr[3];
algorithm
  y := x + 1 "stmt comment";
  if x > 0.5 then
    z := 1;
  elseif x > 0.2 then
    z := 2;
  else
    z := 3;
  end if;
  if x > 2 then
    z := 5;
  end if;
  s := 0;
  for i in 1:3 loop
    s := s + i;
    arr[i] := s;
  end for;
  for i in 1:2:5 loop
    s := s + i;
  end for;
  for e in {1.5, 2.5} loop
    s := s + e;
  end for;
  k := 0;
  while k < 3 loop
    k := k + 1;
    if k == 2 then
      break;
    end if;
  end while;
  (a, b) := f2(x);
  (, b) := f2(x);
  (a, ) := f2(x);
  when x < 0.5 then
    d := pre(d) + 1;
  elsewhen x < 0.3 then
    d := 0;
  end when;
  assert(x > -1, "msg");
  when x < 0.05 then
    terminate("bye");
  end when;
equation
  der(x) = -x;
end P08_statements;
