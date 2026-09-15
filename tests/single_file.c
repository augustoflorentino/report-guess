#define INFR_IMPLEMENTATION
#include "infer/infer.h"

int main(void)
{
    unsigned char report[2];
    unsigned char desc[128];
    infr_session_t *s = NULL;
    size_t len = 0;
    int status;
    int i;

    status = infr_create(sizeof(report), 64, NULL, NULL, NULL, &s);
    for (i = 0; status == INFR_OK && i < 64; i++) {
        report[0] = (unsigned char)(i % 8 < 4);
        report[1] = (unsigned char)(i * 4);
        status = infr_add(s, i < 8 ? INFR_REST : INFR_FREE, report, sizeof(report));
    }
    if (status == INFR_OK)
        status = infr_analyze(s);
    if (status == INFR_OK)
        status = infr_descriptor(s, 0, desc, sizeof(desc), &len);
    if (infr_field_count(s) == 0 || infr_field(s, 0) == NULL || infr_strerror(status) == NULL)
        status = INFR_ERR_STATE;
    infr_destroy(s);
    return status == INFR_OK && len > 0 ? 0 : 1;
}
