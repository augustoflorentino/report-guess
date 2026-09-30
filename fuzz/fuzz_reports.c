#include "infer/infer.h"

#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    unsigned char out[512];
    unsigned char desc[4096];
    infr_session_t *s = NULL;
    size_t report_len;
    size_t len = 0;
    size_t pos = 1;

    if (size < 2)
        return 0;
    report_len = data[0] < 224 ? (size_t)(data[0] % 32) + 1 : (size_t)(data[0] - 224) * 8 + 33;
    if (infr_create(report_len, 512, NULL, NULL, NULL, &s) != INFR_OK)
        return 0;
    while (pos + 1 + report_len <= size) {
        int label = (int)(data[pos] % 8) - 1;

        if (infr_add(s, label, data + pos + 1, report_len) != INFR_OK)
            break;
        if (label > 0)
            infr_set_usage(s, label, data[pos], data[pos + 1]);
        pos += 1 + report_len;
    }
    if (infr_analyze(s) == INFR_OK) {
        infr_normalize(s, data + 1, report_len, out, sizeof(out));
        infr_descriptor(s, data[1] % 2, desc, sizeof(desc), &len);
        infr_descriptor(s, 0, NULL, 0, &len);
    }
    infr_destroy(s);
    return 0;
}
