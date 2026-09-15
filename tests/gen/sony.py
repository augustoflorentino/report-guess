import math
import random
import sys
import zlib

from hidtools.device import base_gamepad, sony_gamepad as sony

DEVICES = {
    'ds4-usb': (sony.PS4ControllerUSB, sony.PS4ControllerUSB),
    'ds4-bt': (sony.PS4ControllerBluetooth, sony.PS4ControllerUSB),
    'ds5-usb': (sony.PS5ControllerUSB, sony.PS5ControllerUSB),
    'ds5-bt': (sony.PS5ControllerBluetooth, sony.PS5ControllerUSB),
}
HARDWARE_FIELDS = {sony.PS4ControllerUSB: 0, sony.PS4ControllerBluetooth: 2}
HAT = [0, 1, 2, 3, 4, 5, 6, 7]


class Session:
    def __init__(self, device, rng):
        self.device = device
        self.rng = rng
        self.samples = []
        self.left = [128, 128]
        self.right = [128, 128]
        self.gyro = [0, 0, 0]
        self.counter = 0
        self.timestamp = 0
        self.hardware = type(device) in HARDWARE_FIELDS

    def add_hardware_fields(self, report):
        shift = HARDWARE_FIELDS[type(self.device)]
        self.counter = (self.counter + 1) % 64
        self.timestamp = (self.timestamp + 188 + self.rng.randint(-2, 2)) % 65536
        report[7 + shift] = (report[7 + shift] & 0x03) | (self.counter << 2)
        report[10 + shift] = self.timestamp & 0xff
        report[11 + shift] = self.timestamp >> 8
        if len(report) == 78:
            crc = zlib.crc32(bytes(report[0:74]), zlib.crc32(bytes([0xa1])))
            report[74:78] = crc.to_bytes(4, 'little')

    def jitter(self, v):
        return max(0, min(255, int(round(v)) + self.rng.choice((-1, 0, 0, 0, 1))))

    def emit(self, label, n=1, hat=None, buttons=None):
        for _ in range(n):
            report = self.device.create_report(
                left=(self.jitter(self.left[0]), self.jitter(self.left[1])),
                right=(self.jitter(self.right[0]), self.jitter(self.right[1])),
                hat_switch=hat, buttons=buttons or {},
                accel=(self.rng.randint(-20, 20), 9800 + self.rng.randint(-20, 20), self.rng.randint(-20, 20)),
                gyro=tuple(g + self.rng.randint(-3, 3) for g in self.gyro))
            report = list(report)
            if self.hardware:
                self.add_hardware_fields(report)
            self.samples.append((label, bytes(report)))


def buttons_of(device):
    valid = []
    for b in range(1, 33):
        try:
            device.create_report(buttons={b: False})
        except base_gamepad.InvalidHIDCommunication:
            continue
        valid.append(b)
    return valid


def build(cls, rng):
    device = cls()
    s = Session(device, rng)
    label = 1
    s.emit(0, 300)
    for b in buttons_of(device):
        for _ in range(4):
            s.emit(label, 12, buttons={b: True})
            s.emit(label, 12, buttons={b: False})
        label += 1
    for _ in range(3):
        for d in HAT:
            s.emit(label, 10, hat=d)
        s.emit(label, 10, hat=None)
    s.emit(0, 1, hat=None)
    label += 1
    for stick, axis in ((s.left, 0), (s.left, 1), (s.right, 0), (s.right, 1)):
        for i in range(200):
            stick[axis] = 128 + 127 * math.sin(2 * math.pi * 2 * i / 200)
            s.emit(label)
        stick[axis] = 128
        s.emit(label, 10)
        label += 1
    s.emit(0, 100)
    return s.samples


def kind_of(field):
    if field.size == 1:
        return 'button'
    if field.size == 4 and field.logical_max == 7:
        return 'hat'
    return 'axis'


def report_fields(device, report_id):
    report = device.parsed_rdesc.input_reports.get(report_id)
    if report is None:
        return []
    return [f for f in report.fields if not f.is_const and (f.usage_page >> 16) < 0xff00]


def truth(device, layout):
    fields = report_fields(layout, 1)
    shift = 0
    if type(device) is not type(layout):
        probe = bytes(device.create_report(left=(17, 34), right=(51, 68)))
        shift = (probe.index(bytes([17, 34, 51, 68])) - fields[0].start // 8) * 8
    lines = []
    for f in fields:
        for i in range(f.count):
            start = f.start + shift + i * f.size
            lines.append('%d %d %s page=%x' % (start, f.size, kind_of(f), f.usage_page >> 16))
    if type(device) in HARDWARE_FIELDS:
        base = HARDWARE_FIELDS[type(device)] * 8
        lines.append('%d 6 counter' % (base + 58))
        lines.append('%d 16 counter' % (base + 80))
    if len(device.create_report()) == 78:
        lines.append('592 32 checksum')
    return lines


def main():
    out = sys.argv[1]
    for name, (cls, layout) in DEVICES.items():
        rng = random.Random(name)
        samples = build(cls, rng)
        with open('%s/%s.truth' % (out, name), 'w') as f:
            f.write('\n'.join(truth(cls(), layout())) + '\n')
        with open('%s/%s.samples' % (out, name), 'w') as f:
            f.write('# hid-tools %s simulator (%s), labeled actions\n' % (cls.__name__, name))
            for label, report in samples:
                f.write('%d %s\n' % (label, report.hex()))


if __name__ == '__main__':
    main()
