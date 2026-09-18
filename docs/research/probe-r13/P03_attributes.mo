type Voltage = Real(final quantity = "ElectricPotential", final unit = "V", displayUnit = "kV");
type MyInt = Integer(min = 0, max = 10);
type Color = enumeration(red "r", green, blue);

model P03_attributes
  // attributes written in deliberately scrambled order
  Real a(stateSelect = StateSelect.prefer, nominal = 10, fixed = true, start = 1, max = 100, min = -100, displayUnit = "mm", unit = "m", quantity = "Length");
  Voltage v(start = 0.5);
  Voltage v2(displayUnit = "mV", nominal = 1e-3);
  Integer i(start = 2, fixed = true, max = 5, min = 1, quantity = "count");
  MyInt mi(start = 3);
  Boolean b(fixed = true, start = false, quantity = "flag");
  String s(start = "abc", quantity = "name");
  Color c(start = Color.green, fixed = true, max = Color.blue, min = Color.red, quantity = "col");
  Real u(unbounded = true, start = 2);
  Real ss1(stateSelect = StateSelect.never);
  Real ss2(stateSelect = StateSelect.avoid);
  Real ss3(stateSelect = StateSelect.default);
  Real ss4(stateSelect = StateSelect.always);
  parameter Real p(start = 1.5, fixed = false);
  parameter Real q(min = 0) = 2 "with binding and attr";
  Real r(start = q + 1);
equation
  der(a) = 1;
  v = 1; v2 = 1;
  when time > 1 then
    i = 3; mi = 4; b = true; s = "x"; c = Color.red;
  end when;
  u = 1; ss1 = 1; ss2 = 1; ss3 = 1; ss4 = 1; r = 1;
initial equation
  p = 3;
end P03_attributes;
