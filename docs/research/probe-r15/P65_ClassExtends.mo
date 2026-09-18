model A
  replaceable model B
    Real x = 1.0;
  end B;
  B b;
end A;

model P65_ClassExtends
  extends A;
  redeclare model extends B
    Real y = 2.0;
  end B;
  B b2;
end P65_ClassExtends;
