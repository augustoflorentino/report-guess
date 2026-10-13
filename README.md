# infer

Infer the field layout of undocumented binary reports, such as those of a gamepad that speaks
raw HID without a usable descriptor, and emit a HID report descriptor that describes them.

Record reports while the device rests and while you perform known actions, label each report
with the action, and `infr_analyze` finds:

- rest noise: bits and fields that change while nothing is touched
- buttons: single bits, active-high or active-low
- continuous fields: width, sign, endianness, observed range, rest value (center) and noise
- hats: 4-bit direction fields with a null state
- counters (fixed step or timestamp-like), checksums (sum8, xor8, crc8, crc8 Maxim, crc32 with
  an optional seed byte) and constant runs

Each field gets the label of the action that moves it most, and a HID usage (buttons on the
Button page, axes on Generic Desktop X..Wheel, hats as Hat Switch) that `infr_set_usage` can
override per label. `infr_descriptor` writes a gamepad descriptor with one input field per
inferred button, axis and hat at its bit position; counters, checksums, noise and constants become padding. Reports
must go through `infr_normalize` (active-low buttons inverted, big-endian fields swapped) before
they match that descriptor.

Devices that send several reports, each starting with its report ID byte, are analyzed in one
session created with `INFR_REPORT_IDS` in `infr_options_t.flags`. `infr_add` routes each report
by its first byte, each report ID keeps its own length (up to the `report_len` given to
`infr_create`) and its own layout, `infr_field` lists the fields of every report in ID order,
and `infr_descriptor` writes one input report per ID.

All memory is allocated by `infr_create`, except in `INFR_REPORT_IDS` sessions, where the first
report of each ID allocates that report's buffers (`max_samples` reports each); analysis never
allocates.

## Build and install

```sh
cmake -S . -B build -DBUILD_SHARED_LIBS=ON
cmake --build build
cmake --install build --prefix /usr/local
```

Use it from CMake:

```cmake
find_package(infer REQUIRED)
target_link_libraries(app PRIVATE infer::infer)
```

Or with pkg-config:

```sh
cc app.c $(pkg-config --cflags --libs infer)
```

## Single-file form

`scripts/amalgamate.sh` writes `dist/infer/infer.h`. Copy it into your project and, in exactly
one `.c` file:

```c
#define INFR_IMPLEMENTATION
#include "infer/infer.h"
```

Define `INFR_STATIC` as well to keep every function private to that file.

## Usage

See [`examples/basic.c`](examples/basic.c), and
[`examples/samples.c`](examples/samples.c) for a tool that reads a samples file (one report per
line: label, then the report in hex; lines starting with `#` are skipped) and prints the layout
and the descriptor (`infer_samples file ids` for reports that start with a report ID). The header `include/infer/infer.h` is the
API reference.

Conventions:

- every function that can fail returns `INFR_OK` or an error code; `infr_strerror` describes it
- what `infr_create` returns is released only by `infr_destroy`, which accepts `NULL`
- functions never take ownership of their arguments; returned pointers belong to the library
- pass an `infr_options_t` with your allocator to `infr_create`, or `NULL` for `malloc`/`free`

Labels: `INFR_REST` (0) marks reports taken with the device untouched, `INFR_FREE` (-1) reports
of free use, and positive numbers one action each. A field moved by no particular action has
label `INFR_NO_LABEL`.

Fields (`infr_field_t`, layout fixed for the 2.x series):

| member | meaning |
|---|---|
| `report_id` | report the field belongs to; 0 without `INFR_REPORT_IDS` |
| `bit_offset`, `bit_size` | position in the report, bit 0 is the low bit of byte 0 (the ID byte, when there is one) |
| `kind` | `INFR_CONSTANT`, `INFR_BUTTON`, `INFR_AXIS`, `INFR_HAT`, `INFR_COUNTER`, `INFR_CHECKSUM`, `INFR_NOISE` |
| `flags` | `INFR_SIGNED`, `INFR_BIG_ENDIAN`, `INFR_ACTIVE_LOW` |
| `logical_min`, `logical_max` | observed range; for hats the direction range; for counters the wrap value |
| `rest`, `noise` | value at rest and its spread while untouched |
| `label` | the action that moves the field, or `INFR_NO_LABEL` |
| `usage_page`, `usage` | HID usage written to the descriptor |
| `algorithm`, `covered_len`, `seed` | checksums: `INFR_SUM8` to `INFR_CRC32`, bytes covered from the start, crc32 seed byte or -1 |
| `step` | counters: increment per report |

Limits:

- at most `INFR_MAX_LABELS` (64) distinct positive labels per session; `infr_add` returns
  `INFR_ERR_FULL` past that
- at most `INFR_MAX_HINTS` usages set with `infr_set_usage`; usage page 0 is rejected
- one checksum per report, which must be the last bytes it covers
- hats use values 0 to 7 plus one rest value of 8 or 15
- fields up to 32 bits; 32-bit axes are always signed
- at most `INFR_MAX_REPORTS` (16) report IDs per session; report ID 0 is rejected

## Validation

`tests/data` holds labeled sessions and the expected analysis, checked by `ctest`:

- `ninja-*`: Gamer Ninja Sword (Home+X mode) reports built from its known mapping, labeled and
  free play
- `multi`: two input reports with different IDs and lengths in one session
- `ds4-*`, `ds5-*`: hid-tools DualShock 4 and DualSense simulators over USB and Bluetooth, with
  the DualShock 4 counter and timestamp filled in as the hardware does

`tests/gen` regenerates them (`sony.py` needs hid-tools), `check.py` compares an analysis with
the field layout taken from the device descriptor, and `decode.py` parses the emitted
descriptor with hid-tools and checks it against the inferred fields.

When hid-parser 3.0 or later is found by CMake, `ctest` also parses every emitted descriptor with
`hidp_desc_parse` in strict mode and checks each data field against the analysis. It also replays every sample through `infr_normalize` and
`hidp_report_decode` and checks each decoded value against the raw report.

## Versioning

Semantic Versioning. Compatible releases bump the minor version; incompatible ones bump the
major version and the soname. See [CHANGELOG.md](CHANGELOG.md).

## License

MIT, see [LICENSE](LICENSE).
