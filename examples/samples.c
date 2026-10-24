#include <report-guess/report-guess.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REPORT 4096
#define MAX_SAMPLES 100000

static const char *const kinds[] = {"constant", "button", "axis", "hat", "counter", "checksum", "noise"};

static size_t read_hex(const char *text, unsigned char *out)
{
    size_t n = 0;
    unsigned v;

    while (n < MAX_REPORT && sscanf(text + n * 2, "%2x", &v) == 1)
        out[n++] = (unsigned char)v;
    return n;
}

static int load(FILE *f, const infr_options_t *options, infr_session_t **out_session)
{
    static char line[MAX_REPORT * 2 + 64];
    static char hex[MAX_REPORT * 2 + 1];
    static unsigned char report[MAX_REPORT];
    infr_session_t *s = NULL;
    int label;
    int status = INFR_OK;

    while (status == INFR_OK && fgets(line, sizeof(line), f) != NULL) {
        size_t len;

        if (line[0] == '#' || sscanf(line, "%d %8192s", &label, hex) != 2)
            continue;
        len = read_hex(hex, report);
        if (s == NULL)
            status = infr_create(options->flags ? MAX_REPORT : len, MAX_SAMPLES, options, &s);
        if (status == INFR_OK)
            status = infr_add(s, label, report, len);
    }
    *out_session = s;
    return s == NULL ? INFR_ERR_SAMPLES : status;
}

static void print_fields(const infr_model_t *s)
{
    size_t i;

    for (i = 0; i < infr_field_count(s); i++) {
        const infr_field_t *f = infr_field(s, i);

        printf("report %-3u bit %-5zu %2u bits  %-8s label %-3d usage %04x:%04x  range %d..%d\n", f->report_id,
               f->bit_offset, f->bit_size, kinds[f->kind], f->label, (unsigned)f->usage_page, (unsigned)f->usage,
               f->logical_min, f->logical_max);
    }
}

static int print_descriptor(const infr_model_t *s)
{
    static unsigned char desc[65536];
    size_t len = 0;
    size_t i;
    int status = infr_descriptor(s, desc, sizeof(desc), &len);

    if (status != INFR_OK)
        return status;
    for (i = 0; i < len; i++)
        printf("%02x%s", desc[i], i + 1 == len ? "\n" : " ");
    return INFR_OK;
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "r") : stdin;
    infr_options_t options = {0, NULL, NULL, NULL};
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    int status;

    if (f == NULL) {
        fprintf(stderr, "usage: infer_samples [samples file] [ids]\n");
        return 2;
    }
    if (argc > 2 && strcmp(argv[2], "ids") == 0)
        options.flags = INFR_REPORT_IDS;
    status = load(f, &options, &s);
    if (status == INFR_OK)
        status = infr_analyze(s, &m);
    infr_destroy(s);
    if (status == INFR_OK) {
        print_fields(m);
        status = print_descriptor(m);
    }
    if (status != INFR_OK)
        fprintf(stderr, "report-guess: %s\n", infr_strerror(status));
    infr_model_destroy(m);
    if (f != stdin)
        fclose(f);
    return status == INFR_OK ? 0 : 1;
}
