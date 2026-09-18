model IFBR
  Real x, y;
equation
  if time > 1 then
    x = 1;
    y = 2;
  else
    x = 3;
  end if;
end IFBR;
