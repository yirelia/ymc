package A
  model B
    model C
      Real x;
    end C;
  end B;
  constant B b;
end A;
model P25
  import A.b.C;
  C c;
end P25;
