#include "hid-parser/hid-parser.h"
#include "infer/infer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 16384
#define MAX_REPORT (MAX_LINE / 2)

typedef struct {
    const infr_session_t *session;
    const unsigned char *raw;
    size_t checked;
    int ok;
} decode_ctx_t;

static size_t parse_hex(const char *text, unsigned char *out, size_t cap)
{
    size_t n = 0;
    unsigned v;

    while (n < cap && sscanf(text, "%2x", &v) == 1) {
        out[n++] = (unsigned char)v;
        text += 2;
    }
    return n;
}

static uint32_t raw_bits(const unsigned char *r, const infr_field_t *f)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < f->bit_size; i++) {
        size_t bit = f->bit_offset + i;

        if (f->flags & INFR_BIG_ENDIAN)
            bit = f->bit_offset + (f->bit_size / 8 - 1 - i / 8) * 8 + i % 8;
        v |= (uint32_t)((r[bit >> 3] >> (bit & 7)) & 1u) << i;
    }
    return v;
}

static int32_t expected_value(const unsigned char *r, const infr_field_t *f)
{
    uint32_t v = raw_bits(r, f);
    uint32_t sign = f->bit_size < 32 ? 1u << (f->bit_size - 1) : 0x80000000u;

    if (f->kind == INFR_BUTTON && (f->flags & INFR_ACTIVE_LOW))
        return (int32_t)(v ^ 1u);
    if (!(f->flags & INFR_SIGNED))
        return (int32_t)v;
    return (int32_t)((int64_t)(v ^ sign) - (int64_t)sign);
}

static const infr_field_t *field_at(const infr_session_t *s, unsigned report_id, size_t bit_offset)
{
    size_t i;

    for (i = 0; i < infr_field_count(s); i++) {
        if (infr_field(s, i)->report_id == report_id && infr_field(s, i)->bit_offset == bit_offset)
            return infr_field(s, i);
    }
    return NULL;
}

static int on_value(void *opaque, const hidp_value_t *v)
{
    decode_ctx_t *c = opaque;
    const infr_field_t *f = field_at(c->session, v->field->report_id, v->field->bit_offset);

    if (f == NULL || (int64_t)expected_value(c->raw, f) != v->value) {
        fprintf(stderr, "bit %zu decoded %lld\n", v->field->bit_offset, (long long)v->value);
        c->ok = 0;
        return 1;
    }
    c->checked++;
    return 0;
}

static infr_session_t *load(FILE *f, unsigned flags)
{
    static char line[MAX_LINE];
    static char hex[MAX_LINE];
    static unsigned char report[MAX_REPORT];
    infr_options_t options = {0, NULL, NULL, NULL};
    infr_session_t *s = NULL;
    int label;

    options.flags = flags;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len;

        if (line[0] == '#' || sscanf(line, "%d %16383s", &label, hex) != 2)
            continue;
        len = parse_hex(hex, report, sizeof(report));
        if (s == NULL && infr_create(len, 100000, &options, &s) != INFR_OK)
            return NULL;
        if (infr_add(s, label, report, len) != INFR_OK)
            break;
    }
    if (s != NULL && infr_analyze(s) == INFR_OK)
        return s;
    infr_destroy(s);
    return NULL;
}

static hidp_desc_t *describe(const infr_session_t *s)
{
    hidp_parse_options_t strict = {HIDP_PARSE_STRICT, NULL, NULL, NULL};
    static unsigned char desc[65536];
    hidp_desc_t *d = NULL;
    size_t len = 0;
    size_t error_offset = 0;

    if (infr_descriptor(s, desc, sizeof(desc), &len) != INFR_OK)
        return NULL;
    if (hidp_desc_parse(desc, len, &strict, &d, &error_offset) != HIDP_OK)
        return NULL;
    return d;
}

static int replay(FILE *f, const infr_session_t *s, const hidp_desc_t *d, size_t *out_checked)
{
    static char line[MAX_LINE];
    static char hex[MAX_LINE];
    static unsigned char raw[MAX_REPORT];
    static unsigned char norm[MAX_REPORT];
    decode_ctx_t c = {NULL, NULL, 0, 1};
    int label;

    c.session = s;
    c.raw = raw;
    while (c.ok && fgets(line, sizeof(line), f) != NULL) {
        size_t len;

        if (line[0] == '#' || sscanf(line, "%d %16383s", &label, hex) != 2)
            continue;
        len = parse_hex(hex, raw, sizeof(raw));
        if (infr_normalize(s, raw, len, norm, sizeof(norm)) != INFR_OK)
            return 0;
        if (hidp_report_decode(d, HIDP_INPUT, norm, len, on_value, &c) != HIDP_OK)
            c.ok = 0;
    }
    *out_checked = c.checked;
    return c.ok;
}

int main(int argc, char **argv)
{
    FILE *f;
    infr_session_t *s;
    hidp_desc_t *d = NULL;
    size_t checked = 0;
    int ok = 0;

    if (argc != 3 || (f = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "usage: decode_check <samples> <uses report ids: 0 or 1>\n");
        return 2;
    }
    s = load(f, strtoul(argv[2], NULL, 0) != 0 ? INFR_REPORT_IDS : 0);
    if (s != NULL)
        d = describe(s);
    if (d != NULL) {
        rewind(f);
        ok = replay(f, s, d, &checked);
    }
    fclose(f);
    hidp_desc_destroy(d);
    infr_destroy(s);
    printf("%zu values checked\n", checked);
    return ok && checked > 0 ? 0 : 1;
}
