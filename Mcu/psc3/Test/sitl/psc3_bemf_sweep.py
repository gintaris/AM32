#!/usr/bin/env python3
'''
Throttle sweep in the AM32 SITL, to compare the PSC3 BEMF sensing
(make AM32_SITL_CAN SITL_PSC3_BEMF=1, Mcu/SITL/Src/sitl_psc3_bemf.c) with
the STM32 comparator emulation of the standard SITL build.

Needs the ESCSim repository (https://github.com/am32-firmware/ESCSim) for
the SITL input/state helpers:

  ESCSIM=~/ESCSim python3 psc3_bemf_sweep.py --sitl SITL.elf [--config MODEL.json]

For every DShot throttle step it waits a few seconds of simulation time and
prints the mean rpm and whether the motor runs, so the steps where the
motor stalls or desyncs stand out.
'''

import argparse
import os
import sys
import time

ESCSIM = os.path.expanduser(os.environ.get('ESCSIM', '~/ESCSim'))
sys.path.insert(0, os.path.join(ESCSIM, 'SITL'))

import run_ci_tests as ci            # noqa: E402  Sitl, Sender, sim_sleep, rpm_from_state
import sitl_dshot as sd              # noqa: E402
from sitl_gui_backend import SimStream  # noqa: E402

STEPS = [60, 100, 150, 200, 300, 400, 600, 800, 1000, 1300, 1600, 2000]


def sweep(sitl, config, steps, dwell):
    args = ['--can-uri', 'none', '--input-type', '1']
    if config:
        args += ['--config', config]
    rows = []
    with ci.Sitl(sitl, args):
        sim = SimStream('127.0.0.1', ci.STATE_PORT, period_us=200)
        sim.enabled = True
        tx = ci.Sender(sd.TYPE_DSHOT600)
        try:
            ci.sim_sleep(sim, 2.2)       # arm at zero throttle
            for value in steps:
                tx.value = value
                ci.sim_sleep(sim, dwell)
                rows.append((value, ci.rpm_from_state(sim, 1.0)))
            tx.value = 0
            ci.sim_sleep(sim, 2.0)
            rows.append((0, ci.rpm_from_state(sim, 0.3)))
        finally:
            tx.stop()
            sim.close()
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sitl', required=True, nargs='+', help='SITL binaries to compare')
    ap.add_argument('--config', help='SITL JSON model (motor, battery, esc, sim)')
    ap.add_argument('--dwell', type=float, default=3.0, help='simulation seconds per step')
    ap.add_argument('--steps', help='comma separated DShot values (default %s)'
                    % ','.join(str(v) for v in STEPS))
    args = ap.parse_args()
    steps = [int(v) for v in args.steps.split(',')] if args.steps else STEPS

    results = {}
    for s in args.sitl:
        t0 = time.time()
        results[s] = sweep(s, args.config, steps, args.dwell)
        print('%s: %.0f s wall' % (os.path.basename(s), time.time() - t0), file=sys.stderr)

    names = [os.path.basename(s) for s in args.sitl]
    print('dshot  ' + '  '.join('%22s' % n for n in names))
    for i, (value, _) in enumerate(results[args.sitl[0]]):
        cells = []
        for s in args.sitl:
            rpm = results[s][i][1]
            state = 'stopped' if rpm < 300 else 'runs'
            cells.append('%8.0f rpm %-9s' % (rpm, state))
        print('%5d  ' % value + '  '.join('%22s' % c for c in cells))


if __name__ == '__main__':
    main()
