import sys

from hidtools.hid import ReportDescriptor


def main():
    inferred = {}
    descriptor = None
    report_len = int(sys.argv[2])
    for line in open(sys.argv[1]):
        parts = line.split()
        if parts[0] == 'descriptor':
            descriptor = bytes.fromhex(parts[1])
        elif parts[2] in ('button', 'axis', 'hat'):
            inferred[(int(parts[0]), int(parts[1]))] = parts[2]
    rdesc = ReportDescriptor.from_bytes(descriptor)
    reports = list(rdesc.input_reports.values())
    errors = []
    if len(reports) != 1:
        errors.append('expected one input report, got %d' % len(reports))
    report = reports[0]
    if report.size != report_len:
        errors.append('report size %d, samples have %d' % (report.size, report_len))
    declared = {(f.start, f.size) for f in report.fields if not f.is_const}
    if declared != set(inferred):
        errors.append('fields differ: %s' % sorted(declared ^ set(inferred)))
    for e in errors:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
