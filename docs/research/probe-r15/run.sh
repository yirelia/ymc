#!/bin/sh
# 用法: sh run.sh P01_ModScope.mo [...]   —— 在 probe-r15 目录内跑安装版 omc 1.25.0，输出存 <名>.out
for f in "$@"; do
  b="${f%.mo}"
  timeout 120 omc "$f" > "$b.out" 2>&1
  echo "=== $f (exit $?)"; cat "$b.out"
done
