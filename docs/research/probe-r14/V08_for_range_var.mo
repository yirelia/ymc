model V08_for_range_var
  Integer n = 2;
  Real x[3];
equation
  for i in 1:n loop
    x[i] = i;
  end for;
end V08_for_range_var;
