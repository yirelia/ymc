record R
  model M
    constant Real x = 1;
  end M;
  Real r = 0;
end R;
package P
  constant R c;
end P;
model P31
  Real y = P.c.M.x;
end P31;
