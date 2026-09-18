// 短类定义修饰的作用域 = 短类所在的类
package Pk
  constant Real c = 42;
  model A
    parameter Real p = 1;
    constant Real c = -1;
  end A;
  model S = A(p = c);     // c -> Pk.c
end Pk;

model P03_ModScopeShort
  constant Real c = 1000;
  Pk.S s;
end P03_ModScopeShort;
