#include "infer/infer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 16384

static const char *const kinds[] = {"constant", "button", "axis", "hat", "counter", "checksum", "noise"};

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

static int load(FILE *f, infr_session_t **out, size_t max_samples, const infr_options_t *options)
{
    static char line[MAX_LINE];
    static unsigned char report[MAX_LINE / 2];
    infr_session_t *s = NULL;
    int label;
    char hex[MAX_LINE];

    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len;

        if (line[0] == '#' || sscanf(line, "%d %16383s", &label, hex) != 2)
            continue;
        len = parse_hex(hex, report, sizeof(report));
        if (s == NULL && infr_create(len, max_samples, options, &s) != INFR_OK)
            return 0;
        if (infr_add(s, label, report, len) != INFR_OK)
            return 0;
    }
    *out = s;
    return s != NULL;
}

static void print_flags(unsigned flags)
{
    printf("%s%s%s%s%s", flags & INFR_SIGNED ? " signed" : "", flags & INFR_BIG_ENDIAN ? " be" : "",
           flags & INFR_ACTIVE_LOW ? " active-low" : "", flags & INFR_NOISY ? " noisy" : "",
           flags & INFR_NULL_STATE ? " null" : "");
}

static void print_field(const infr_field_t *f)
{
    printf("%zu %u %s", f->bit_offset, f->bit_size, kinds[f->kind]);
    print_flags(f->flags);
    printf(" range=%ld..%ld rest=%ld noise=%ld label=%d usage=%lx:%lx", (long)f->logical_min,
           (long)f->logical_max, (long)f->rest, (long)f->noise, f->label, (unsigned long)f->usage_page,
           (unsigned long)f->usage);
    if (f->kind == INFR_CHECKSUM)
        printf(" algorithm=%u covered=%zu seed=%d", f->algorithm, f->covered_len, f->seed);
    if (f->kind == INFR_COUNTER)
        printf(" step=%ld", (long)f->step);
    printf("\n");
}

int main(int argc, char **argv)
{
    static unsigned char desc[65536];
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    infr_options_t options = {0, NULL, NULL, NULL};
    FILE *f = argc > 1 ? fopen(argv[1], "r") : NULL;
    size_t i;
    size_t len = 0;
    int status;

    if (argc > 2 && strtoul(argv[2], NULL, 0) != 0)
        options.flags = INFR_REPORT_IDS;
    if (f == NULL || !load(f, &s, 1u << 20, &options)) {
        fprintf(stderr, "usage: infer_run <samples> [report_id]\n");
        return 2;
    }
    fclose(f);
    status = infr_analyze(s, &m);
    infr_destroy(s);
    if (status == INFR_OK)
        status = infr_descriptor(m, desc, sizeof(desc), &len);
    if (status != INFR_OK) {
        fprintf(stderr, "error: %s\n", infr_strerror(status));
        infr_model_destroy(m);
        return 1;
    }
    for (i = 0; i < infr_field_count(m); i++)
        print_field(infr_field(m, i));
    printf("descriptor ");
    for (i = 0; i < len; i++)
        printf("%02x", desc[i]);
    printf("\n");
    infr_model_destroy(m);
    return 0;
}
