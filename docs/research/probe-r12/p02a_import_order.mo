package P
  constant Real c = 1;
end P;
package Q
  constant Real c = 2;
end Q;
model P02a
  import P.*;
  import Q.c;
  Real a = c;
end P02a;
