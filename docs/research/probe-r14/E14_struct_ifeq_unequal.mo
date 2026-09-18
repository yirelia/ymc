model E14_struct_ifeq_unequal
  parameter Boolean flag = false;
  parameter Real p = 3;
  Real x;
  Real y;
equation
  if flag then
    x = p;
  else
    x = 2 * p;
    y = 1;
  end if;
  if not flag then
    y = time;
  end if;
end E14_struct_ifeq_unequal;
