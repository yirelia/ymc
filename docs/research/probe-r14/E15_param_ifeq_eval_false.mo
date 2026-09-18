model E15_param_ifeq_eval_false
  parameter Boolean flag = true annotation(Evaluate = false);
  parameter Real p = 3;
  Real x;
equation
  if flag then
    x = p;
  else
    x = 2 * p;
  end if;
end E15_param_ifeq_eval_false;
