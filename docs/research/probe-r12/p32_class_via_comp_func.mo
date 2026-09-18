record R
  package M
    function f
      output Real y = 1.0;
    end f;
  end M;
  Real r = 0;
end R;
package P
  constant R c;
end P;
model P32
  Real y = P.c.M.f();
end P32;
