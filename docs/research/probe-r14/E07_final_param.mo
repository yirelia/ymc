model E07_final_param
  final parameter Real a = 2.0;
  final parameter Real b = a * 3;
  final constant Real k = 7;
  parameter Real c = a + k;
  Real x(final start = a);
equation
  der(x) = a + b + c;
end E07_final_param;
