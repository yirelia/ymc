model A
  replaceable Real x = 1.0;
end A;

model P66_RedeclareElement
  extends A;
  redeclare Real x = 2.0;
end P66_RedeclareElement;
