# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## 0.3.0 - 2026-09-29

- added: `INFR_MAX_LABELS`; `infr_add` returns `INFR_ERR_FULL` past 64 distinct positive labels
  instead of silently dropping their statistics
- fixed: a trailing byte that happened to match xor8, or two bytes always equal, were taken as
  checksums; a checksum now has to cover at least two varying bytes and only one is reported
- fixed: crc32 with a seed byte is searched in one pass over the seeds instead of one pass per
  seed and position
- fixed: 32-bit axes overflowed their range and value; they are now signed with the full range
- fixed: `infr_descriptor` with a report ID accepted layouts whose first byte was not that ID
- fixed: hats with rest 15 accepted value 8 as a direction
- fixed: counters stepping by more than 1 were missed; hopeless counter scans stop early
- fixed: `infr_set_usage` accepted usage page 0
- fixed: wide noisy fields could overflow `noise`
- descriptors write global items only when they change (about half the size) and split padding
  above 4096 bits
- automatic usages are assigned in linear time
- tests: regression tests for each fix; with hid-parser installed, `ctest` checks every emitted
  descriptor with `hidp_desc_parse`
- fuzz target covers reports up to 281 bytes

Updating: no code changes needed. Sessions with more than 64 action labels now get
`INFR_ERR_FULL`, `infr_set_usage` with page 0 gets `INFR_ERR_ARG`, and `infr_descriptor` with a
report ID that is not the first constant byte gets `INFR_ERR_ARG`. Emitted descriptors are
shorter but describe the same reports.

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
