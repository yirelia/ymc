record R "record R"
  Real x "x comment";
  Integer n = 1;
  parameter Real p = 2.0;
  constant Real c = 3.0;
  final Real fin = 4.0;
  Real arr[2];
end R;

record Small
  Real a;
  Real b;
end Small;

function mk
  input Real u;
  output Small s;
algorithm
  s := Small(u, 2 * u);
end mk;

function useR
  input R r;
  output Real y;
algorithm
  y := r.x + r.arr[1];
end useR;

model P10_records
  Small s1 = Small(1, 2);
  Small s2 = mk(time);
  Small s3;
  parameter Small ps = Small(a = 3, b = 4);
  R r(x = 1, arr = {1, 2});
  Real y = useR(r);
equation
  s3 = mk(2 * time);
end P10_records;
