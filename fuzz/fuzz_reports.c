#include "infer/infer.h"

#include <stdint.h>

static size_t fuzz_len(const uint8_t *report, size_t report_len, int ids)
{
    size_t cut = ids ? report[0] % 4u : 0;

    return cut < report_len ? report_len - cut : report_len;
}

static void fuzz_read(const infr_model_t *s, const uint8_t *report, size_t len)
{
    int64_t value;
    size_t i;

    for (i = 0; i < infr_field_count(s); i++)
        infr_read(s, infr_field(s, i), report, len, &value);
}

static void fuzz_usages(infr_model_t *m, const uint8_t *data, size_t size, size_t report_len)
{
    size_t pos;

    for (pos = 2; pos + 1 + report_len <= size; pos += 1 + report_len) {
        int label = (int)(data[pos] % 8) - 1;

        if (label > 0)
            infr_set_usage(m, label, data[pos], data[pos + 1]);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char out[512];
    unsigned char desc[4096];
    infr_options_t options = {0, NULL, NULL, NULL};
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    size_t report_len;
    size_t len = 0;
    size_t pos = 2;

    if (size < 3)
        return 0;
    report_len = data[0] < 224 ? (size_t)(data[0] % 32) + 1 : (size_t)(data[0] - 224) * 8 + 33;
    options.flags = data[1] & INFR_REPORT_IDS;
    if (infr_create(report_len, 512, &options, &s) != INFR_OK)
        return 0;
    while (pos + 1 + report_len <= size) {
        int label = (int)(data[pos] % 8) - 1;
        size_t n = fuzz_len(data + pos + 1, report_len, (int)options.flags);

        if (infr_add(s, label, data + pos + 1, n) == INFR_ERR_FULL)
            break;
        pos += 1 + report_len;
    }
    for (len = 0; len <= infr_report_count(s); len++)
        infr_report_info(s, len, NULL, NULL, NULL);
    len = 0;
    if (infr_analyze(s, &m) == INFR_OK) {
        if (data[1] & 2)
            infr_destroy(s), s = NULL;
        fuzz_usages(m, data, size, report_len);
        infr_normalize(m, data + 2, fuzz_len(data + 2, report_len, (int)options.flags), out, sizeof(out));
        fuzz_read(m, data + 2, fuzz_len(data + 2, report_len, (int)options.flags));
        infr_set_usage(m, 1, 0x09, data[2]);
        infr_descriptor(m, desc, sizeof(desc), &len);
        infr_descriptor(m, NULL, 0, &len);
    }
    infr_model_destroy(m);
    infr_destroy(s);
    return 0;
}
