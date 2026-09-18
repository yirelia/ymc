model A
  Real x = 1;
end A;
model B
  Real x = 2;
end B;
model P32_DupNotIdentical
  extends A;
  extends B;
end P32_DupNotIdentical;
