package A
  constant Real c = 1;
  model M
    package A
      constant Real c = 2;
    end A;
    Real x = A.c;
    Real y = .A.c;
  end M;
end A;
model P06
  A.M m;
end P06;
