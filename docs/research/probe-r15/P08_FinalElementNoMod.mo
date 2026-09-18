// final 元素本身没有修饰也不允许被修饰
model A
  final parameter Real p;
end A;

model P08_FinalElementNoMod
  A a(p = 3);
end P08_FinalElementNoMod;
