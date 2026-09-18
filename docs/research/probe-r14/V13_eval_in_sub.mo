model V13_eval_in_sub
  model A
    parameter Real k = 1 annotation(Evaluate = true);
    parameter Real g = 2;
    Real y = k * g;
  end A;
  parameter Real top = 5;
  A a1;
  A a2(k = top);
  A a3(k = 7, g = top);
end V13_eval_in_sub;
