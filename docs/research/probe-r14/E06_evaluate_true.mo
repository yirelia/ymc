model E06_evaluate_true
  parameter Real a = 2.0 annotation(Evaluate = true);
  parameter Real b = a * 3;
  parameter Real c = 4.0 annotation(Evaluate = false);
  parameter Real d = c + 1;
  parameter Real e = 2 * f annotation(Evaluate = true);
  parameter Real f = 5;
  parameter Real h = 1.0;
  Real x;
equation
  x = a + b + c + d + e + f + h;
end E06_evaluate_true;
