#include "infer_int.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INFR_MAX_REPORT 4096

static void *infr_default_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return malloc(size);
}

static void infr_default_free(void *ctx, void *ptr)
{
    (void)ctx;
    free(ptr);
}

static void *infr_take(infr_session_t *s, size_t count, size_t size, int *failed)
{
    void *p;

    if (*failed || count > SIZE_MAX / size) {
        *failed = 1;
        return NULL;
    }
    p = s->alloc_fn(s->alloc_ctx, count * size);
    if (p == NULL)
        *failed = 1;
    else
        memset(p, 0, count * size);
    return p;
}

static void infr_release(infr_session_t *s, void *p)
{
    if (p != NULL)
        s->free_fn(s->alloc_ctx, p);
}

static int infr_alloc_arrays(infr_session_t *s)
{
    int failed = 0;

    s->reports = infr_take(s, s->max_samples, s->report_len, &failed);
    s->labels = infr_take(s, s->max_samples, sizeof(int), &failed);
    s->fields = infr_take(s, s->bits, sizeof(infr_field_t), &failed);
    s->bit_changes = infr_take(s, s->bits, sizeof(uint32_t), &failed);
    s->bit_rest_changes = infr_take(s, s->bits, sizeof(uint32_t), &failed);
    s->bit_rest_ones = infr_take(s, s->bits, sizeof(uint32_t), &failed);
    s->bit_label = infr_take(s, s->bits, sizeof(int), &failed);
    s->bit_owner = infr_take(s, s->bits, 1, &failed);
    return failed ? INFR_ERR_NOMEM : INFR_OK;
}

int infr_create(size_t report_len, size_t max_samples, infr_alloc_fn alloc_fn,
                infr_free_fn free_fn, void *alloc_ctx, infr_session_t **out_session)
{
    infr_session_t *s;

    if (out_session == NULL || (alloc_fn == NULL) != (free_fn == NULL))
        return INFR_ERR_ARG;
    *out_session = NULL;
    if (report_len == 0 || report_len > INFR_MAX_REPORT || max_samples < 2 || max_samples > UINT32_MAX)
        return INFR_ERR_ARG;
    if (alloc_fn == NULL) {
        alloc_fn = infr_default_alloc;
        free_fn = infr_default_free;
    }
    s = alloc_fn(alloc_ctx, sizeof(*s));
    if (s == NULL)
        return INFR_ERR_NOMEM;
    memset(s, 0, sizeof(*s));
    s->alloc_fn = alloc_fn;
    s->free_fn = free_fn;
    s->alloc_ctx = alloc_ctx;
    s->report_len = report_len;
    s->bits = report_len * 8;
    s->max_samples = max_samples;
    if (infr_alloc_arrays(s) != INFR_OK) {
        infr_destroy(s);
        return INFR_ERR_NOMEM;
    }
    *out_session = s;
    return INFR_OK;
}

void infr_destroy(infr_session_t *session)
{
    if (session == NULL)
        return;
    infr_release(session, session->reports);
    infr_release(session, session->labels);
    infr_release(session, session->fields);
    infr_release(session, session->bit_changes);
    infr_release(session, session->bit_rest_changes);
    infr_release(session, session->bit_rest_ones);
    infr_release(session, session->bit_label);
    infr_release(session, session->bit_owner);
    session->free_fn(session->alloc_ctx, session);
}

int infr_add(infr_session_t *session, int label, const unsigned char *report, size_t report_len)
{
    if (session == NULL || report == NULL || label < INFR_FREE)
        return INFR_ERR_ARG;
    if (report_len != session->report_len)
        return INFR_ERR_LEN;
    if (session->count == session->max_samples)
        return INFR_ERR_FULL;
    memcpy(session->reports + session->count * session->report_len, report, report_len);
    session->labels[session->count++] = label;
    session->analyzed = 0;
    return INFR_OK;
}

int infr_set_usage(infr_session_t *session, int label, uint32_t usage_page, uint32_t usage)
{
    size_t i;

    if (session == NULL || label <= INFR_REST || usage_page > 0xffff || usage > 0xffff)
        return INFR_ERR_ARG;
    for (i = 0; i < session->hint_count; i++) {
        if (session->hints[i].label == label)
            break;
    }
    if (i == INFR_MAX_HINTS)
        return INFR_ERR_FULL;
    session->hints[i].label = label;
    session->hints[i].usage_page = usage_page;
    session->hints[i].usage = usage;
    if (i == session->hint_count)
        session->hint_count++;
    session->analyzed = 0;
    return INFR_OK;
}

int infr_analyze(infr_session_t *session)
{
    if (session == NULL)
        return INFR_ERR_ARG;
    if (session->count < 2)
        return INFR_ERR_SAMPLES;
    session->field_count = 0;
    memset(session->bit_owner, 0, session->bits);
    infr_collect_stats(session);
    infr_find_counters(session);
    infr_find_checksums(session);
    infr_find_big_endian(session);
    infr_find_hats(session);
    infr_find_chains(session);
    infr_add_constants(session);
    infr_sort_fields(session);
    infr_assign_usages(session);
    session->analyzed = 1;
    return INFR_OK;
}

size_t infr_field_count(const infr_session_t *session)
{
    return session != NULL && session->analyzed ? session->field_count : 0;
}

const infr_field_t *infr_field(const infr_session_t *session, size_t index)
{
    if (session == NULL || !session->analyzed || index >= session->field_count)
        return NULL;
    return &session->fields[index];
}

static void infr_swap_bytes(unsigned char *p, size_t len)
{
    size_t i;

    for (i = 0; i < len / 2; i++) {
        unsigned char t = p[i];

        p[i] = p[len - 1 - i];
        p[len - 1 - i] = t;
    }
}

int infr_normalize(const infr_session_t *session, const unsigned char *report, size_t report_len,
                   unsigned char *out, size_t out_len)
{
    size_t i;

    if (session == NULL || report == NULL || out == NULL)
        return INFR_ERR_ARG;
    if (!session->analyzed)
        return INFR_ERR_STATE;
    if (report_len != session->report_len)
        return INFR_ERR_LEN;
    if (out_len < report_len)
        return INFR_ERR_SPACE;
    memmove(out, report, report_len);
    for (i = 0; i < session->field_count; i++) {
        const infr_field_t *f = &session->fields[i];

        if (f->kind == INFR_BUTTON && (f->flags & INFR_ACTIVE_LOW))
            out[f->bit_offset >> 3] ^= (unsigned char)(1u << (f->bit_offset & 7));
        if (f->flags & INFR_BIG_ENDIAN)
            infr_swap_bytes(out + (f->bit_offset >> 3), f->bit_size / 8);
    }
    return INFR_OK;
}

const char *infr_strerror(int status)
{
    switch (status) {
    case INFR_OK:
        return "ok";
    case INFR_ERR_ARG:
        return "invalid argument";
    case INFR_ERR_NOMEM:
        return "out of memory";
    case INFR_ERR_LEN:
        return "report length differs from the session";
    case INFR_ERR_FULL:
        return "no room left";
    case INFR_ERR_SAMPLES:
        return "not enough samples";
    case INFR_ERR_STATE:
        return "analyze the session first";
    case INFR_ERR_SPACE:
        return "buffer too small";
    default:
        return "unknown status";
    }
}
