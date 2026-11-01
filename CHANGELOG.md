# Changelog

Each release lists what changed, what updating means for existing code, and every security fix.

## Unreleased

## 4.0.0 - 2026-10-31

- breaking: `infr_field_t.logical_min`, `logical_max`, `rest` and `noise` are `int64_t`. A
  32-bit counter now reports its real wrap value 4294967295 instead of `INT32_MAX`, and a 32-bit
  unsigned field reports values above `INT32_MAX` without truncation
- fixed: 32-bit axes are no longer forced to signed; an unsigned 32-bit axis gets Logical
  Minimum 0 and Logical Maximum 0xffffffff in the descriptor, which Linux and hid-parser read as
  unsigned
- `noise` is no longer clamped to `INT32_MAX`
- `infr_field_t` is 104 bytes on 64-bit, fixed for 4.x
- SONAME is now `libreport-guess.so.4`

Upgrading from 3.x:

- code that stores `logical_min`, `logical_max`, `rest` or `noise` in an `int` or `int32_t` must
  use `int64_t`; printf formats need `%lld` with a `long long` cast (or `PRId64`)
- code that relied on 32-bit axes always having `INFR_SIGNED` must read the flag
- relink: the ABI changed

## 3.1.1 - 2026-10-23

- renamed: the library is now report-guess, to avoid clashing with Facebook Infer. The API, the
  `infr_` prefix and the ABI are unchanged

Upgrading:

- include `<report-guess/report-guess.h>` instead of `<infer/infer.h>`
- CMake: `find_package(report-guess)` and link `report-guess::report-guess`
- pkg-config: `report-guess`; link with `-lreport-guess`; the shared library is `libreport-guess.so.3`
- single file: `dist/report-guess/report-guess.h`
- CMake options are `REPORT_GUESS_BUILD_TESTS`, `REPORT_GUESS_BUILD_EXAMPLES` and `REPORT_GUESS_BUILD_FUZZ`
- remove an installed infer 3.x (`libinfer.so.3`, `include/infer`, `lib/cmake/infer`,
  `infer.pc`) after relinking

## 3.1.0 - 2026-10-23

- added: `infr_model_report_count` and `infr_model_report_info` list the reports a model
  describes, with their ID and length, so a program that keeps only the model can size its
  buffers and route reports without the session

Updating: no code changes needed.

## 3.0.0 - 2026-10-23

- changed: `infr_analyze(session, &model)` returns a new `infr_model_t` holding the inferred
  layout; the session keeps only the samples. The model does not depend on the session, so a
  program can destroy the session and its `max_samples` x `report_len` sample buffers once the
  analysis is done and keep only the model, which holds the fields of each report
- changed: `infr_set_usage`, `infr_field_count`, `infr_field`, `infr_normalize`, `infr_read` and
  `infr_descriptor` take the model instead of the session
- added: `infr_model_destroy`
- changed: usages set with `infr_set_usage` belong to the model and apply at once; each call
  to `infr_analyze` returns a model with the default usages
- changed: `INFR_ERR_STATE` is no longer returned; a model is always analyzed
- SONAME is now `libinfer.so.3`

Upgrading from 2.x:

- after the samples are added, call `infr_analyze(session, &model)` and pass `model` to
  `infr_field_count`, `infr_field`, `infr_normalize`, `infr_read` and `infr_descriptor`
- move `infr_set_usage` calls after `infr_analyze`, on the model
- release the model with `infr_model_destroy`; the session can be destroyed before it
- code that checked for `INFR_ERR_STATE` can drop that check
- rebuild against the new header; binaries linked to `libinfer.so.2` do not load `libinfer.so.3`

## 2.2.1 - 2026-10-23

- fixed: in a report ID session, `infr_add` rejecting a 65th label for a new report ID left an
  empty report behind, and every later `infr_analyze` failed with `INFR_ERR_SAMPLES`
- fixed: a report ID seen only once made `infr_analyze` fail for the whole session; such IDs
  are now left out of the fields and the descriptor, and `infr_normalize` treats them as
  unknown, until they have two samples

Updating: no code changes needed.

## 2.2.0 - 2026-10-19

- added: `infr_read` gives the value of one field in a raw report, with sign, byte order and
  active-low applied, as `int64_t` so unsigned 32-bit fields keep their value
- added: `infr_report_count` and `infr_report_info` list the report IDs of a session with
  their length and sample count
- tests: the hid-parser cross-check also compares every decoded value with `infr_read`

Updating: no code changes needed. 2.1.0 was not released; its additions are part of this
release.

## 2.0.0 - 2026-10-12

- added: `INFR_REPORT_IDS` sessions: reports that start with a report ID byte are routed by it,
  each ID keeps its own length and layout, and `infr_descriptor` writes one input report per ID
  (up to `INFR_MAX_REPORTS`, 16). Devices that mix several input reports (Bluetooth gamepads,
  vendor reports next to the main one) no longer need one session per ID and a hand-merged
  descriptor
- changed: `infr_options_t` starts with `unsigned flags`
- changed: `infr_descriptor` lost its `report_id` argument; the report ID now comes from the
  session
- changed: `infr_field_t` gains `report_id` as its first member (80 bytes on 64-bit, 64 on
  32-bit, fixed for 2.x)
- `infr_normalize` routes reports the same way and returns `INFR_ERR_LEN` for an unknown ID
- tests: `multi` dataset with two report IDs of different lengths, decoded with hid-parser
- soname is now `libinfer.so.2`

Upgrading:

- options: `{alloc_fn, free_fn, ctx}` becomes `{0, alloc_fn, free_fn, ctx}`
- `infr_descriptor(s, 0, out, out_len, &n)` becomes `infr_descriptor(s, out, out_len, &n)`
- `infr_descriptor(s, id, ...)` with a report ID: create the session with
  `infr_options_t o = {INFR_REPORT_IDS, NULL, NULL, NULL};` and call `infr_descriptor(s, out,
  out_len, &n)`; reports of other IDs that used to fail `infr_add` with `INFR_ERR_LEN` are now
  accepted and described
- code reading `infr_field_t` by member name needs no change; code that depends on its size or
  offsets must be rebuilt
- rebuild against the new header; binaries linked to `libinfer.so.1` keep using the old library

## 1.1.0 - 2026-10-04

- added: `infr_set_usage` after `infr_analyze` applies the usages at once without a new
  analysis; the session stays analyzed
- added: `examples/samples.c`, a tool that analyzes a samples file and prints the layout and the
  descriptor
- tests: the optional hid-parser checks use hid-parser 3.x and its 64-bit decoded values

Updating: no code changes needed. Code that called `infr_analyze` again after `infr_set_usage`
keeps working; the call is no longer needed.

## 1.0.0 - 2026-10-04

- changed: `infr_create` takes `const infr_options_t *` (allocator and context) instead of three
  separate arguments, so new options can be added without another break
- changed: `infr_descriptor` names its buffer `out`/`out_len` and the written length
  `out_written`, following the output-last naming of the rest of the API
- `infr_field_t` layout is fixed for the 1.x series and its members are documented in the README
- tests: with hid-parser 1.0 installed, every sample is decoded with `hidp_report_decode` and
  compared with the raw report
- soname is now `libinfer.so.1`

Upgrading:

- `infr_create(len, max, NULL, NULL, NULL, &s)` becomes `infr_create(len, max, NULL, &s)`
- with an allocator: `infr_options_t o = {alloc_fn, free_fn, ctx};` then
  `infr_create(len, max, &o, &s)`
- `infr_descriptor` calls keep their arguments; only parameter names changed
- rebuild against the new header; binaries linked to `libinfer.so.0` keep using the old library

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
