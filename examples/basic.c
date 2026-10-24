#include <report-guess/report-guess.h>

#include <stdio.h>

static const char *const kinds[] = {"constant", "button", "axis", "hat", "counter", "checksum", "noise"};

static void make_report(int i, unsigned char *report)
{
    report[0] = (unsigned char)i;
    report[1] = (unsigned char)(i >= 20 && i < 60 && i % 10 < 5 ? 0xfe : 0xff);
    report[2] = (unsigned char)(i >= 60 && i < 100 ? 68 + (i - 60) * 3 : 128);
}

int main(void)
{
    unsigned char report[3];
    unsigned char desc[256];
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    size_t len = 0;
    size_t i;
    int status;
    int n;

    status = infr_create(sizeof(report), 120, NULL, &s);
    for (n = 0; status == INFR_OK && n < 120; n++) {
        make_report(n, report);
        status = infr_add(s, n < 20 || n >= 100 ? INFR_REST : n < 60 ? 1 : 2, report, sizeof(report));
    }
    if (status == INFR_OK)
        status = infr_analyze(s, &m);
    infr_destroy(s);
    if (status == INFR_OK)
        status = infr_set_usage(m, 1, 0x09, 1);
    if (status == INFR_OK)
        status = infr_descriptor(m, desc, sizeof(desc), &len);
    if (status != INFR_OK) {
        fprintf(stderr, "error: %s\n", infr_strerror(status));
        infr_model_destroy(m);
        return 1;
    }
    for (i = 0; i < infr_field_count(m); i++) {
        const infr_field_t *f = infr_field(m, i);

        printf("bit %zu, %u bits: %s, label %d\n", f->bit_offset, f->bit_size, kinds[f->kind], f->label);
    }
    printf("descriptor:");
    for (i = 0; i < len; i++)
        printf(" %02x", desc[i]);
    printf("\n");
    infr_model_destroy(m);
    return 0;
}
