model E04_struct_cond
  parameter Boolean use = true;
  parameter Boolean other = false;
  parameter Boolean c2 = use and not other;
  Real a if use;
  Real b if other;
  Real c if c2;
  Real x;
equation
  if use then
    x = 1;
  else
    x = 2;
  end if;
  a = 1;
  c = if use then 3 else 4;
end E04_struct_cond;
