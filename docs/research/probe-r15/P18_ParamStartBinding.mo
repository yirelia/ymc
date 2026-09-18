// 无绑定的 parameter 用 start 作绑定（updateParameterBinding）+ each 展开
model A
  parameter Real p(start = 1);
  parameter Real w[2](each start = 3);
end A;

model P18_ParamStartBinding
  A a;
  A b[2](p(start = {5, 6}));
end P18_ParamStartBinding;
