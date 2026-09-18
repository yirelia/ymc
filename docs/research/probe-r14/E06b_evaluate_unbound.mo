model E06b_evaluate_unbound
  parameter Real g(start = 1.0) annotation(Evaluate = true);
  parameter Real h(fixed = false) = 3 annotation(Evaluate = true);
  Real x;
equation
  x = g + h;
end E06b_evaluate_unbound;
