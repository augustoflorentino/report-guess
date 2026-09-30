#include "hid-parser/hid-parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 4096
#define MAX_DESC 65536

typedef struct {
    size_t bit_offset;
    unsigned bit_size;
    long min;
    long max;
    unsigned long page;
    unsigned long usage;
    int hat;
} expected_t;

static expected_t expected[MAX_FIELDS];
static size_t expected_len;
static unsigned char desc[MAX_DESC];
static size_t desc_len;

static int parse_line(const char *line)
{
    expected_t *e = &expected[expected_len];
    char kind[16];
    const char *range = strstr(line, "range=");
    const char *usage = strstr(line, "usage=");

    if (sscanf(line, "%zu %u %15s", &e->bit_offset, &e->bit_size, kind) != 3)
        return 0;
    if (strcmp(kind, "button") != 0 && strcmp(kind, "axis") != 0 && strcmp(kind, "hat") != 0)
        return 1;
    if (range == NULL || usage == NULL || expected_len == MAX_FIELDS)
        return 0;
    if (sscanf(usage, "usage=%lx:%lx", &e->page, &e->usage) != 2)
        return 0;
    e->hat = strcmp(kind, "hat") == 0;
    if (e->hat && sscanf(range, "range=%ld..%ld", &e->min, &e->max) != 2)
        return 0;
    expected_len++;
    return 1;
}

static int load(const char *path)
{
    static char line[MAX_DESC * 2 + 64];
    FILE *f = fopen(path, "r");
    int ok = f != NULL;

    while (ok && fgets(line, sizeof(line), f) != NULL) {
        unsigned v;
        const char *p = line + strlen("descriptor ");

        if (strncmp(line, "descriptor ", 11) != 0) {
            ok = parse_line(line);
            continue;
        }
        while (desc_len < MAX_DESC && sscanf(p, "%2x", &v) == 1) {
            desc[desc_len++] = (unsigned char)v;
            p += 2;
        }
    }
    if (f != NULL)
        fclose(f);
    return ok && desc_len > 0;
}

static const hidp_field_t *find(const hidp_desc_t *d, size_t bit_offset)
{
    size_t i;

    for (i = 0; i < hidp_desc_field_count(d); i++) {
        const hidp_field_t *f = hidp_desc_field(d, i);

        if (!(f->flags & HIDP_FLAG_CONSTANT) && f->bit_offset == bit_offset)
            return f;
    }
    return NULL;
}

static int matches(const expected_t *e, const hidp_field_t *f)
{
    if (f->report_size != e->bit_size || f->report_count != 1 || f->usage_page != e->page)
        return 0;
    if (f->usages_len != 1 || f->usages[0].min != (e->page << 16 | e->usage))
        return 0;
    if (!(f->flags & HIDP_FLAG_VARIABLE))
        return 0;
    if (e->hat)
        return (f->flags & HIDP_FLAG_NULL_STATE) && f->logical_min == e->min && f->logical_max == e->max;
    return 1;
}

static int check(const hidp_desc_t *d, unsigned report_id, size_t report_len)
{
    size_t data_fields = 0;
    size_t i;
    const hidp_report_t *r = hidp_desc_find_report(d, HIDP_INPUT, report_id);

    if (r == NULL || hidp_desc_report_count(d) != 1 || hidp_desc_warning_count(d) != 0) {
        fprintf(stderr, "expected one input report and no warnings\n");
        return 0;
    }
    if (r->byte_len != report_len) {
        fprintf(stderr, "report is %zu bytes, samples have %zu\n", r->byte_len, report_len);
        return 0;
    }
    for (i = 0; i < expected_len; i++) {
        const hidp_field_t *f = find(d, expected[i].bit_offset);

        if (f == NULL || !matches(&expected[i], f)) {
            fprintf(stderr, "field at bit %zu does not match\n", expected[i].bit_offset);
            return 0;
        }
    }
    for (i = 0; i < hidp_desc_field_count(d); i++)
        data_fields += !(hidp_desc_field(d, i)->flags & HIDP_FLAG_CONSTANT);
    if (data_fields != expected_len) {
        fprintf(stderr, "%zu data fields, expected %zu\n", data_fields, expected_len);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    hidp_parse_options_t strict = {HIDP_PARSE_STRICT, NULL, NULL, NULL};
    hidp_desc_t *d = NULL;
    size_t error_offset = 0;
    unsigned report_id;
    size_t report_len;
    int ok;

    if (argc != 4 || !load(argv[1])) {
        fprintf(stderr, "usage: descriptor_check <infer_run output> <report_id> <report_len>\n");
        return 2;
    }
    report_id = (unsigned)strtoul(argv[2], NULL, 0);
    report_len = (size_t)strtoul(argv[3], NULL, 0);
    if (hidp_desc_parse(desc, desc_len, &strict, &d, &error_offset) != HIDP_OK) {
        fprintf(stderr, "hidp_desc_parse failed at offset %zu\n", error_offset);
        return 1;
    }
    ok = check(d, report_id, report_len);
    hidp_desc_destroy(d);
    return ok ? 0 : 1;
}
