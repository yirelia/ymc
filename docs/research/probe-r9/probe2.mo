package Probe2
  encapsulated partial package EP
  end EP;
  operator record OR
    Real re;
  end OR;
  expandable connector EC
  end EC;
  pure function PF
    input Real x;
    output Real y;
  algorithm
    y := x;
  end PF;
  impure function IF
    input Real x;
    output Real y;
  algorithm
    y := x;
  end IF;
  model Base2
    parameter Real k2 = 0;
  end Base2;
  model Dup
    extends Base2;
    extends Base2(k2 = 1);
    Real z1(nominal(), start = 1) "empty submod nominal()";
    Real z2(start := 2) "assign binding";
    Real z3(final unit = "m", each start = 3);
    Real z4[:];
    redeclare Real q = 3;
    replaceable package P = Base2 constrainedby Base2 "cc comment";
  protected
    import Probe2.EP;
  public
    import Probe2.OR;
  end Dup;
end Probe2;
