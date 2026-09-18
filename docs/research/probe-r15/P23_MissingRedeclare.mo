model A
  replaceable model In
    parameter Real k = 1;
  end In;
  In i;
end A;
model In2
  parameter Real k = 2;
end In2;
model P23_MissingRedeclare
  A a(In = In2);
end P23_MissingRedeclare;
