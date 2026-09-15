import sys

DATA_KINDS = ('button', 'axis', 'hat')


def load_samples(path):
    out = []
    for line in open(path):
        if line.startswith('#') or not line.strip():
            continue
        _, hexed = line.split()
        out.append(int.from_bytes(bytes.fromhex(hexed), 'little'))
    return out


def load_truth(path):
    out = {}
    for line in open(path):
        parts = line.split()
        out[(int(parts[0]), int(parts[1]))] = parts[2]
    return out


def load_inferred(path):
    out = {}
    descriptor = None
    for line in open(path):
        parts = line.split()
        if parts[0] == 'descriptor':
            descriptor = bytes.fromhex(parts[1])
            continue
        out[(int(parts[0]), int(parts[1]))] = parts[2]
    return out, descriptor


def varies(samples, off, size):
    mask = (1 << size) - 1
    first = (samples[0] >> off) & mask
    return any(((s >> off) & mask) != first for s in samples)


def check(samples_path, truth_path, inferred_path):
    samples = load_samples(samples_path)
    truth = load_truth(truth_path)
    inferred, descriptor = load_inferred(inferred_path)
    errors = []
    for (off, size), kind in sorted(truth.items()):
        if not varies(samples, off, size):
            continue
        got = inferred.get((off, size))
        if got != kind:
            errors.append('missed %d %d %s (got %s)' % (off, size, kind, got))
    for (off, size), kind in sorted(inferred.items()):
        if kind in DATA_KINDS and (off, size) not in truth:
            errors.append('spurious %d %d %s' % (off, size, kind))
    if descriptor is None:
        errors.append('no descriptor')
    return errors


def main():
    errors = check(*sys.argv[1:4])
    for e in errors:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
