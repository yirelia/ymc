"""把一个探针模型逐行拆成单组件模型，各自让 ymc 与实跑 omc 1.25.0 对拍（加宽时定位"哪一行的形态错了"）。

探针约定：文件里主模型（与文件同名）之前的内容是辅助类，原样保留；主模型体里以 `  parameter` / `  constant` 开头的行
是共用前置声明，放进每个拆出的模型；其余每条缩进行各自成一个模型 S<k>（等式段等多行结构不适用）。

判定（只比 stdout）：PASS 逐字节一致 / SKIP ymc 退出码 66 / FAIL 不一致（打印两边输出）/ ACCEPTS-INVALID omc 报错而
ymc 出了结果 / BOTH-REJECT 两边都拒绝。

用法：python tools/probe_lines.py probe.mo [--ymc build/gcc-debug/ymc.exe] [--out <目录>]
"""
import argparse
import os
import subprocess
import sys


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=120)
    return p.returncode, p.stdout.decode('utf-8', errors='replace'), p.stderr.decode('utf-8', errors='replace')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('probe')
    ap.add_argument('--ymc', default=os.path.join(os.path.dirname(__file__), '..', 'build', 'gcc-debug', 'ymc.exe'))
    ap.add_argument('--omc', default='omc')
    ap.add_argument('--out', help='拆出的模型写到这里（默认与探针同目录）')
    a = ap.parse_args()

    src = open(a.probe, encoding='utf-8').read().splitlines()
    main_name = os.path.splitext(os.path.basename(a.probe))[0]
    start = next((i for i, l in enumerate(src) if l.split()[:2] == ['model', main_name]), None)
    if start is None:
        sys.exit(f'找不到主模型 model {main_name}')
    helpers = src[:start]
    body = [l for l in src[start + 1:] if l.startswith('  ')]
    shared = [l for l in body if l.lstrip().startswith(('parameter ', 'constant '))]
    lines = [l for l in body if l not in shared]
    out = os.path.abspath(a.out or os.path.dirname(os.path.abspath(a.probe)))
    os.makedirs(out, exist_ok=True)
    ymc = os.path.abspath(a.ymc)
    counts = {}
    for k, line in enumerate(lines):
        name = f'S{k}'
        with open(os.path.join(out, name + '.mo'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(helpers + [f'model {name}'] + shared + [line, f'end {name};']) + '\n')
        _, o, _ = run([a.omc, name + '.mo'], out)
        rc, y, ye = run([ymc, name + '.mo'], out)
        omc_err = o.startswith('Error')
        if rc == 66:
            st = 'SKIP'
        elif rc != 0:
            st = 'BOTH-REJECT' if omc_err else 'CRASH'
        elif omc_err:
            st = 'ACCEPTS-INVALID'
        else:
            st = 'PASS' if y == o else 'FAIL'
        counts[st] = counts.get(st, 0) + 1
        note = (ye.strip() or y.strip()).splitlines()[-1] if st == 'SKIP' and (ye.strip() or y.strip()) else ''
        print(f'{st:15s} | {line.strip()}' + (f'   [{note}]' if note else ''))
        if st == 'FAIL':
            print('    omc: ' + o.replace('\n', '\n         '))
            print('    ymc: ' + y.replace('\n', '\n         '))
    print(' '.join(f'{k} {v}' for k, v in sorted(counts.items())))


if __name__ == '__main__':
    main()
