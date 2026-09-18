model T02_binary_types
  Integer i = 3;
  Integer j = i + 2;
  Integer k = i * j;
  Real x = 1.5;
  Real y = x + i;
  Real z = i - x;
  Boolean b = x > i;
  Boolean c = i == j;
  Boolean d = b and not c or i < 2;
  String s = "a" + "b";
  String s2 = s + String(i);
  type E = enumeration(one, two, three);
  E e1 = E.one;
  Boolean be = e1 < E.two;
  Boolean be2 = E.one == E.three;
  Integer ie = Integer(e1);
equation
end T02_binary_types;
