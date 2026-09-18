encapsulated model P45b
  Real x(stateSelect = StateSelect.prefer, start = 1);
  Integer n = Integer(StateSelect.prefer);
  String s = String(n);
equation
  der(x) = -x;
end P45b;
