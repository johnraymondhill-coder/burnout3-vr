#!/usr/bin/env python3
"""controls_split_check.py <ps2|xbox> <mode> <ctl.txt>

The checks of controls_split_scene.sh on the BO3_CTL_LOG of one two-player
run (src/game/controls.c): S a watched word of the title's state changed,
D / D2 player 1's / player 2's driving gate, G a pad's layout, P a pad as
read and as given to the title. The title's port 2 is player 1's pad, port 3
player 2's. Prints PASS/FAIL lines; exit 1 on a failure.

mode: race, roadrage, teamcrush, doubleimpact (the two-player stage, vtable
0x3A939C, both players at once) or partycrash (the one-player stage, 0x3A9ACC,
which goes by player 1 whichever port the pad is on: the ports are reported)."""
import re
import sys

scheme, mode, path = sys.argv[1:4]
STAGE2, STAGE1, FE = 0x3A939C, 0x3A9ACC, 0x3A9E7C
two = mode != 'partycrash'

S, D, D2, G, P = [], [], [], [], []
for line in open(path):
    w = line.split()
    if not w:
        continue
    if w[0] == 'S':
        S.append((int(w[1]), w[2], int(w[3], 16), int(w[5], 16)))
    elif w[0] == 'D':
        D.append((int(w[1]), int(w[3])))
    elif w[0] == 'D2':
        D2.append((int(w[1]), int(w[3])))
    elif w[0] == 'G':
        G.append((int(w[1]), int(w[3]), w[4], int(re.search(r'driving (\d)', line)[1])))
    elif w[0] == 'P':
        m = re.match(r'P (\d+) port (\d) (ps2|xbox)( latched)? read ([0-9A-F]{4}) (.*) -> (.*)$', line)
        f = lambda t: {k: int(v) for k, v in re.findall(r'([a-z]+)(-?\d+)', t)}
        P.append(dict(flip=int(m[1]), port=int(m[2]), layout=m[3], read=f(m[6]), given=f(m[7])))

bad = 0


def check(ok, what):
    global bad
    print(('PASS ' if ok else 'FAIL ') + what)
    bad += not ok


def state_at(series, flip, default=0):
    v = default
    for f, x in series:
        if f <= flip:
            v = x
    return v


def first(name, to):
    for f, n, old, new in S:
        if n == name and new == to:
            return f
    return None


stage_flip = first('module_vt', STAGE2 if two else STAGE1)
check(stage_flip is not None, 'the %s stage module (vtable 0x%X) came up (flip %s)'
      % ('two-player' if two else 'one-player', STAGE2 if two else STAGE1, stage_flip))
if stage_flip is None:
    sys.exit(1)

# Where the gate is on, the module is the stage and nobody has paused: the
# title's pause and the module switch are read by both players' gates.
paused = [(f, new != 0xFFFFFFFF) for f, n, old, new in S if n == 'paused_by']
vt = [(f, new) for f, n, old, new in S if n == 'module_vt']
on = [f for f, d in D if d] + [f for f, d in D2 if d]
check(all(state_at(vt, f) in (STAGE1, STAGE2) and not state_at(paused, f) for f in on),
      'a gate is on only in a stage module with nobody paused')

if scheme == 'xbox':
    check(not [g for g in G if g[2] == 'ps2'], 'xbox scheme: no pad was ever given in the PS2 layout')
    check(all(p['given'] == p['read'] for p in P), 'xbox scheme: every pad change given as read')
    sys.exit(1 if bad else 0)

PORTS = (2, 3)
ps2_at = {}
for port in PORTS:
    ons = [g for g in G if g[1] == port and g[2] == 'ps2' and g[0] >= stage_flip]
    ps2_at[port] = ons[0][0] if ons else None
if two:
    for port in PORTS:
        check(ps2_at[port] is not None, 'player %d (port %d): the PS2 layout came on in the stage (flip %s)'
              % (port - 1, port, ps2_at[port]))
    check(bool(D) and bool(D2), 'both gates were logged (D, D2)')
    check(any(d for f, d in D) and any(d for f, d in D2), 'both players had their gate on')
else:
    for port in PORTS:
        print('NOTE port %d: PS2 layout %s in the stage'
              % (port, 'came on at flip %s' % ps2_at[port] if ps2_at[port] else 'never came on'))
    check(any(ps2_at.values()), 'the PS2 layout came on for at least one pad')

# The layout is given only where that player's gate is on, and is off again
# when it goes off. A G line says its pad's own gate.
check(all(g[3] == 1 for g in G if g[2] == 'ps2'), 'every PS2 layout switch was made with the gate on')
for port in PORTS:
    if ps2_at[port] is None:
        continue
    gate = D if port == 2 else D2
    # Per pad: an xbox switch follows each gate-off after a PS2 one.
    flips = sorted(g[0] for g in G if g[1] == port)
    seq = [(g[0], g[2]) for g in sorted(G, key=lambda g: g[0]) if g[1] == port]
    ok = all(state_at(gate, f) == (1 if lay == 'ps2' else 0) or (not two and port == 3)
             for f, lay in seq)
    check(ok, 'port %d: each layout switch agrees with the gate of its own player (%d switches)'
          % (port, len(seq)))

# The mapping itself: bottom button (a) is the accelerator (rt), R2 (rt) is
# the crashbreaker (b), as bo3_ctl_ps2 says.
for port in PORTS:
    if ps2_at[port] is None:
        continue
    rows = [p for p in P if p['port'] == port and p['layout'] == 'ps2'
            and p['read'].get('a', 0) == 255 and p['read'].get('rt', 0) == 255]
    check(bool(rows) and all(r['given'].get('a') == 0 and r['given'].get('rt') == 255
                             and r['given'].get('b') == 255 for r in rows),
          'port %d: in the PS2 layout the bottom button reaches the title as the right trigger, '
          'the right trigger as B (%d reads)' % (port, len(rows)))

# A player whose event is over (results up) is back on the menu layout while
# the other may still drive.
for k, port in ((1, 2), (2, 3)):
    f = None
    for f2, n, old, new in S:
        if n == 'p%d_results' % k and new & 0xFF and not old & 0xFF:      # the results byte
            f = f2
            break
    if f is None or ps2_at[port] is None:
        print('NOTE player %d: no results seen in this run' % k)
        continue
    layout = None
    for g in sorted(G, key=lambda g: g[0]):
        if g[1] == port and g[0] <= f + 2:
            layout = g[2]
    check(layout == 'xbox', 'player %d: results up at flip %d, port %d back on the menu layout' % (k, f, port))

sys.exit(1 if bad else 0)
