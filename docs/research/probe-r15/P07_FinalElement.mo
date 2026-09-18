// final 元素：patchElementModFinal 把声明上的修饰标成 final
model A
  final parameter Real p = 1;
  final Real x(start = 1.0);
end A;

model P07_FinalElement
  A a(x(start = 2.0));
end P07_FinalElement;
