connector C
  Real e;
  flow Real f;
  stream Real s;
end C;

model Inner
  outer Real g;
  Real v = g;
end Inner;

model P02_prefixes
  inner Real g = 9.81;
  Inner sub;
  C c;
  input Real u;
  output Real yo;
  discrete Real d;
  parameter Real p1 = 1.0;
  final parameter Real p2 = 2.0;
  constant Real k = 3.0;
  final constant Integer n = 2;
  parameter Integer m = 3 annotation(Evaluate = true);
  Real arr[n];
  discrete input Integer di;
  parameter input Real pin = 1.0;
protected
  parameter Real pp = 4.0;
  final parameter Boolean fb = true;
  constant String s = "str";
  discrete Integer i;
  output Real po;
equation
  yo = u;
  po = u;
  arr = {1.0, 2.0};
  c.e = 1.0;
  c.s = 1.0;
  when time > 1 then
    d = 1.0;
    i = 1;
  end when;
end P02_prefixes;
