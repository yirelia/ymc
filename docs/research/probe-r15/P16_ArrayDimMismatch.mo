model A
  parameter Real p = 0;
end A;

model P16_ArrayDimMismatch
  A a[3](p = {1, 2});
end P16_ArrayDimMismatch;
