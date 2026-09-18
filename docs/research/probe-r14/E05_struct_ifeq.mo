model E05_struct_ifeq
  parameter Boolean flag = true;
  parameter Integer mode = 2;
  parameter Real p = 3;
  Real x;
  Real y;
  Real z;
equation
  if flag then
    x = p;
  else
    x = 2 * p;
  end if;
  if mode == 1 then
    y = 1;
  elseif mode == 2 then
    y = 2;
  else
    y = 3;
  end if;
  z = if flag then p else -p;
end E05_struct_ifeq;
