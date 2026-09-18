#!/bin/sh
# 用法: sh runts.sh ExtendsLoop1 [...]  —— 在 OMC testsuite/flattening/modelica/scodeinst 目录内跑 omc <名>.mo，输出存 probe-r15/T_<名>.out
TS=C:/dev/OpenModelica/testsuite/flattening/modelica/scodeinst
OUT=C:/dev/ymc/docs/research/probe-r15
for n in "$@"; do
  (cd "$TS" && timeout 120 omc "$n.mo" > "$OUT/T_$n.out" 2>&1)
  echo "=== $n (exit $?)"; cat "$OUT/T_$n.out"
done
