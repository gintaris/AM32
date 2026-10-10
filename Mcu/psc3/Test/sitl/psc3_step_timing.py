#!/usr/bin/env python3
'''
Commutation step timing in the AM32 SITL, to compare the PSC3 BEMF sensing
(make AM32_SITL_CAN SITL_PSC3_BEMF=1) with the STM32 comparator emulation.

  ESCSIM=~/ESCSim python3 psc3_step_timing.py --sitl A.elf B.elf [--steps 300,800]

Each SITL runs with SITL_COMM_LOG set, which logs the true electrical rotor
angle of the motor model at every commutation. For every DShot step the
last seconds of the dwell are analysed: the length of each of the six
commutation steps in electrical degrees (60 is ideal) and its spread.
The zero cross inside an odd step is a rising one, inside an even step a
falling one (rising = step % 2 in commutate()), marked r and f in the
table, so a timing problem tied to one BEMF direction shows up as
alternating long and short steps.
'''

import argparse
import math
import os
import sys
import tempfile

ESCSIM = os.path.expanduser(os.environ.get('ESCSIM', '~/ESCSim'))
sys.path.insert(0, os.path.join(ESCSIM, 'SITL'))

import run_ci_tests as ci            # noqa: E402
import sitl_dshot as sd              # noqa: E402
from sitl_gui_backend import SimStream  # noqa: E402

STEPS = [200, 400, 800, 1300, 2000]


def sim_now(sim):
    w = sim.window(0.05)
    return w[-1][0] if w else None


def run(sitl, steps, dwell, settle, log):
    os.environ['SITL_COMM_LOG'] = log
    windows = []
    with ci.Sitl(sitl, ['--can-uri', 'none', '--input-type', '1']):
        sim = SimStream('127.0.0.1', ci.STATE_PORT, period_us=200)
        sim.enabled = True
        tx = ci.Sender(sd.TYPE_DSHOT600)
        try:
            ci.sim_sleep(sim, 2.2)
            for value in steps:
                tx.value = value
                ci.sim_sleep(sim, settle)
                t0 = sim_now(sim)
                ci.sim_sleep(sim, dwell - settle)
                windows.append((value, t0, sim_now(sim), ci.rpm_from_state(sim, 0.5)))
            tx.value = 0
            ci.sim_sleep(sim, 0.5)
        finally:
            tx.stop()
            sim.close()
    del os.environ['SITL_COMM_LOG']
    rows = []
    with open(log) as f:
        for line in f:
            p = line.split()
            if len(p) == 5:
                rows.append((float(p[0]), int(p[1]), float(p[2])))
    return windows, rows


def analyse(rows, t0, t1):
    # length of each step in electrical degrees, by step number
    lengths = {k: [] for k in range(1, 7)}
    sel = [r for r in rows if t0 is not None and t1 is not None and t0 <= r[0] <= t1]
    for (ta, sa, da), (tb, sb, db) in zip(sel, sel[1:]):
        if sb != sa % 6 + 1:
            continue  # not one forward step apart, eg a desync
        lengths[sa].append((db - da) % 360.0)
    out = {}
    for k, v in lengths.items():
        if v:
            mean = sum(v) / len(v)
            sd_ = math.sqrt(sum((x - mean) ** 2 for x in v) / len(v))
            out[k] = (mean, sd_, len(v))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sitl', required=True, nargs='+')
    ap.add_argument('--steps', help='comma separated DShot values')
    ap.add_argument('--dwell', type=float, default=3.0, help='simulation seconds per step')
    ap.add_argument('--settle', type=float, default=1.5, help='seconds skipped after a throttle change')
    args = ap.parse_args()
    steps = [int(v) for v in args.steps.split(',')] if args.steps else STEPS

    for s in args.sitl:
        log = os.path.join(tempfile.gettempdir(), 'comm_%s.log' % os.path.basename(s))
        windows, rows = run(s, steps, args.dwell, args.settle, log)
        print('\n%s  (%d commutations logged)' % (os.path.basename(s), len(rows)))
        print('dshot   rpm   ' + '  '.join('  step %d (%s)' % (k, 'r' if k % 2 else 'f') for k in range(1, 7)))
        for value, t0, t1, rpm in windows:
            st = analyse(rows, t0, t1)
            cells = []
            for k in range(1, 7):
                if k in st:
                    cells.append('%5.1f+-%4.1f' % (st[k][0], st[k][1]))
                else:
                    cells.append('%11s' % '-')
            print('%5d %6.0f  ' % (value, rpm) + '  '.join('%13s' % c for c in cells))


if __name__ == '__main__':
    main()
