// 类修饰（origin = 类）应用到数组组件：需要 fill（NFTyping.mo expandProxySubscripts 注释的例子）
model P17_TypeModFill
  type T = Real[3](start = {1, 2, 3});
  T x[2];
  type S = Real(start = 4);
  S y[2];
end P17_TypeModFill;
