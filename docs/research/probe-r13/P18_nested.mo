model P18_nested
  function noret
    input Real x;
  algorithm
    assert(x > -100, "neg");
  end noret;
  function early
    input Real x;
    output Real y;
  algorithm
    y := x;
    if x > 1 then
      return;
    end if;
    while true loop
      y := y + 1;
      if y > 10 then
        break;
      end if;
    end while;
  end early;
  Real x(start = 1), y, z;
  discrete Real d;
  Boolean c1, c2;
equation
  der(x) = -x;
  c1 = x > 0.5;
  c2 = x > 0.25;
  noret(x) "call comment";
  y = early(x);
  if c1 then
    if c2 then
      z = 1 "inner eq comment";
    else
      z = 2;
    end if "inner if comment";
  else
    z = 3;
  end if "outer if comment";
  when c1 then
    d = 1;
  elsewhen c2 then
    d = 2;
  end when "when comment";
algorithm
  when c1 then
    if c2 then
      for i in 1:2 loop
        noret(x + i);
      end for "for comment";
    end if "if stmt comment";
  end when "when stmt comment";
end P18_nested;
