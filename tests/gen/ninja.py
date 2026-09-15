import math
import random
import sys

BUTTONS = [(0, 3, 'A'), (0, 2, 'B'), (0, 1, 'X'), (0, 0, 'Y'), (2, 6, 'LB'), (0, 6, 'RB'),
           (1, 0, 'SELECT'), (1, 1, 'START'), (1, 3, 'L3'), (1, 2, 'R3'),
           (2, 1, 'UP'), (2, 0, 'DOWN'), (2, 2, 'RIGHT'), (2, 3, 'LEFT')]
STICKS = [(3, 'LX'), (4, 'LY'), (5, 'RX'), (6, 'RY')]
TRIGGERS = [(7, 'LT'), (8, 'RT')]
REPORT_ID = 1


class Pad:
    def __init__(self, rng):
        self.rng = rng
        self.pressed = set()
        self.axes = {pos: 128.0 for pos, _ in STICKS}
        self.triggers = {pos: 0.0 for pos, _ in TRIGGERS}

    def jitter(self, value):
        return value + self.rng.choice((-1, 0, 0, 0, 0, 0, 1))

    def report(self):
        payload = [0xff, 0xff, 0xff] + [0] * 6
        for byte, bit, name in BUTTONS:
            if name in self.pressed:
                payload[byte] &= ~(1 << bit)
        for pos, _ in STICKS:
            value = self.axes[pos]
            if value == 128.0:
                value = self.jitter(128)
            payload[pos] = max(0, min(255, int(round(value))))
        for pos, _ in TRIGGERS:
            payload[pos] = max(0, min(255, int(round(self.triggers[pos]))))
        return bytes([REPORT_ID] + payload)


def emit(out, label, pad, n=1):
    for _ in range(n):
        out.append((label, pad.report()))


def press(out, pad, label, name):
    for _ in range(4):
        pad.pressed.add(name)
        emit(out, label, pad, 15)
        pad.pressed.discard(name)
        emit(out, label, pad, 15)


def sweep(out, pad, label, table, pos, center, amplitude, steps):
    for i in range(steps):
        table[pos] = center + amplitude * math.sin(2 * math.pi * 2 * i / steps)
        emit(out, label, pad)
    table[pos] = center if table is pad.axes else 0.0
    emit(out, label, pad, 10)


def labeled(rng):
    pad = Pad(rng)
    out = []
    label = 1
    emit(out, 0, pad, 300)
    for _, _, name in BUTTONS:
        press(out, pad, label, name)
        label += 1
    for pos, _ in STICKS:
        sweep(out, pad, label, pad.axes, pos, 128, 127.4, 200)
        label += 1
    for pos, _ in TRIGGERS:
        pad.triggers[pos] = 0.0
        for i in range(150):
            pad.triggers[pos] = 255 * math.sin(math.pi * i / 149)
            emit(out, label, pad)
        pad.triggers[pos] = 0.0
        label += 1
    emit(out, 0, pad, 100)
    return out


def free_play(rng, n):
    pad = Pad(rng)
    out = []
    emit(out, 0, pad, 300)
    angle = 0.0
    radius = 0.0
    for i in range(n):
        if rng.random() < 0.05:
            name = rng.choice(BUTTONS)[2]
            pad.pressed ^= {name}
        if i % 400 < 250:
            radius = min(127.0, radius + 4)
            angle += 0.08
        else:
            radius = max(0.0, radius - 8)
        pad.axes[3] = 128 + radius * math.cos(angle) if radius else 128.0
        pad.axes[4] = 128 + radius * math.sin(angle) if radius else 128.0
        pad.axes[5] = 128 + 0.7 * radius * math.sin(1.7 * angle) if radius else 128.0
        pad.axes[6] = 128 + 0.7 * radius * math.cos(1.3 * angle) if radius else 128.0
        pad.triggers[7] = max(0.0, 255 * math.sin(i / 37.0))
        pad.triggers[8] = max(0.0, 255 * math.sin(i / 53.0 + 1))
        emit(out, -1, pad)
    pad.pressed.clear()
    pad.axes = {pos: 128.0 for pos, _ in STICKS}
    pad.triggers = {pos: 0.0 for pos, _ in TRIGGERS}
    emit(out, 0, pad, 100)
    return out


def truth():
    lines = []
    for byte, bit, name in BUTTONS:
        lines.append('%d 1 button active-low %s' % ((byte + 1) * 8 + bit, name))
    for pos, name in STICKS:
        lines.append('%d 8 axis unsigned %s' % ((pos + 1) * 8, name))
    for pos, name in TRIGGERS:
        lines.append('%d 8 axis unsigned %s' % ((pos + 1) * 8, name))
    return lines


def write(path, samples, title):
    with open(path, 'w') as f:
        f.write('# %s\n' % title)
        for label, report in samples:
            f.write('%d %s\n' % (label, report.hex()))


def main():
    out = sys.argv[1]
    rng = random.Random(1)
    write(out + '/ninja-labeled.samples', labeled(rng),
          'synthetic Gamer Ninja Sword (Home+X) reports built from the known mapping, labeled actions')
    write(out + '/ninja-free.samples', free_play(rng, 3000),
          'synthetic Gamer Ninja Sword (Home+X) reports built from the known mapping, free play without labels')
    with open(out + '/ninja.truth', 'w') as f:
        f.write('\n'.join(truth()) + '\n')


if __name__ == '__main__':
    main()
