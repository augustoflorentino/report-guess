#define INFR_IMPLEMENTATION
#include "infer/infer.h"

int main(void)
{
    unsigned char report[2];
    unsigned char desc[128];
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    size_t len = 0;
    int status;
    int i;

    status = infr_create(sizeof(report), 64, NULL, &s);
    for (i = 0; status == INFR_OK && i < 64; i++) {
        report[0] = (unsigned char)(i % 8 < 4);
        report[1] = (unsigned char)(i * 4);
        status = infr_add(s, i < 8 ? INFR_REST : INFR_FREE, report, sizeof(report));
    }
    if (status == INFR_OK)
        status = infr_analyze(s, &m);
    infr_destroy(s);
    if (status == INFR_OK)
        status = infr_descriptor(m, desc, sizeof(desc), &len);
    if (infr_field_count(m) == 0 || infr_field(m, 0) == NULL || infr_strerror(status) == NULL)
        status = INFR_ERR_STATE;
    infr_model_destroy(m);
    return status == INFR_OK && len > 0 ? 0 : 1;
}
