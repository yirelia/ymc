model P01_sections "class comment with \"quote\""
  Real x(start = 1.0);
  Real y;
  Real z;
protected
  Real w;
public
  parameter Real p = 2.0 "param comment";
initial equation
  x = 1.0;
initial algorithm
  z := 0.0;
equation
  der(x) = -p * x;
  w = x;
algorithm
  y := x + 1.0;
algorithm
  z := y * 2.0;
initial equation
  w = 3.0;
  annotation(Documentation(info = "<html>hi</html>"), experiment(StopTime = 2.0));
end P01_sections;
