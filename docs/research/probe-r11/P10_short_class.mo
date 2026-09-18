type Length = Real(unit = "m", min = 0);
type Vec3 = Length[3](each start = 1);
type Color = enumeration(red, green, blue);

model N
  parameter Real p = 1;
  Real y = p * time;
end N;

model M = N(p = 2);

connector RealInput = input Real;

model P10
  Length len = 1.5;
  Vec3 v;
  Length[2] w(each max = 10);
  M m;
  M m3(p = 3);
  Color c = Color.green;
  RealInput u;
  Real t = time;
equation
  v = {1, 2, 3} * time;
  w = {4, 5};
  u = 1;
end P10;
