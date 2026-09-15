# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## 0.1.0 - 2026-09-15

First release.

- `infr_create`, `infr_add`: session holding labeled reports (rest, free use or one action
  per label), all memory allocated up front with an optional user allocator
- `infr_analyze`: finds constants, counters, checksums (sum8, xor8, crc8, crc8 Maxim, crc32
  with seed byte), big-endian 16-bit fields, hats, continuous fields and buttons, with sign,
  active-low, rest value, noise and observed range, and the label that moves each field
- `infr_field_count`, `infr_field`: the inferred layout, sorted by bit offset
- `infr_set_usage`: HID usage per label, overriding the automatic Button/Generic Desktop ones
- `infr_normalize`: rewrites a report so it matches the emitted descriptor
- `infr_descriptor`: HID report descriptor for the layout, with optional report ID
