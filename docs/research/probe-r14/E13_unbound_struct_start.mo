model E13_unbound_struct_start
  parameter Integer n(start = 2);
  parameter Boolean b(start = true);
  Real x[n];
  Real y if b;
end E13_unbound_struct_start;
