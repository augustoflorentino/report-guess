#include "infer_int.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define INFR_MAX_REPORT 4096

typedef char infr_field_size_check[sizeof(infr_field_t) == (sizeof(size_t) == 8 ? 80 : 64) ? 1 : -1];

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

static int infr_read_options(const infr_options_t *options, infr_alloc_t *alloc, unsigned *flags)
{
    alloc->alloc_fn = options != NULL ? options->alloc_fn : NULL;
    alloc->free_fn = options != NULL ? options->free_fn : NULL;
    alloc->alloc_ctx = options != NULL ? options->alloc_ctx : NULL;
    *flags = options != NULL ? options->flags : 0;
    if ((alloc->alloc_fn == NULL) != (alloc->free_fn == NULL) || (*flags & ~INFR_REPORT_IDS) != 0)
        return INFR_ERR_ARG;
    if (alloc->alloc_fn == NULL) {
        alloc->alloc_fn = infr_default_alloc;
        alloc->free_fn = infr_default_free;
    }
    return INFR_OK;
}

static int infr_add_layout(infr_session_t *s, unsigned report_id, size_t report_len, infr_layout_t **out)
{
    size_t i = s->layout_count;
    int status;

    if (s->layout_count == INFR_MAX_REPORTS)
        return INFR_ERR_FULL;
    status = infr_layout_create(&s->alloc, report_len, s->max_samples, &s->no_hints, out);
    if (status != INFR_OK)
        return status;
    (*out)->report_id = report_id;
    for (; i > 0 && s->layouts[i - 1]->report_id > report_id; i--)
        s->layouts[i] = s->layouts[i - 1];
    s->layouts[i] = *out;
    s->layout_count++;
    return INFR_OK;
}

int infr_create(size_t report_len, size_t max_samples, const infr_options_t *options,
                infr_session_t **out_session)
{
    infr_alloc_t alloc;
    infr_layout_t *layout;
    infr_session_t *s;
    unsigned flags;

    if (out_session == NULL || infr_read_options(options, &alloc, &flags) != INFR_OK)
        return INFR_ERR_ARG;
    *out_session = NULL;
    if (report_len == 0 || report_len > INFR_MAX_REPORT || max_samples < 2 || max_samples > UINT32_MAX)
        return INFR_ERR_ARG;
    s = alloc.alloc_fn(alloc.alloc_ctx, sizeof(*s));
    if (s == NULL)
        return INFR_ERR_NOMEM;
    memset(s, 0, sizeof(*s));
    s->alloc = alloc;
    s->flags = flags;
    s->report_len = report_len;
    s->max_samples = max_samples;
    if (!(flags & INFR_REPORT_IDS) && infr_add_layout(s, 0, report_len, &layout) != INFR_OK) {
        infr_destroy(s);
        return INFR_ERR_NOMEM;
    }
    *out_session = s;
    return INFR_OK;
}

void infr_destroy(infr_session_t *session)
{
    size_t i;

    if (session == NULL)
        return;
    for (i = 0; i < session->layout_count; i++)
        infr_layout_destroy(session->layouts[i]);
    session->alloc.free_fn(session->alloc.alloc_ctx, session);
}

static infr_layout_t *infr_find_layout(const infr_session_t *s, const unsigned char *report, size_t report_len)
{
    size_t i;

    if (!(s->flags & INFR_REPORT_IDS))
        return report_len == s->report_len ? s->layouts[0] : NULL;
    for (i = 0; i < s->layout_count; i++) {
        if (s->layouts[i]->report_id == report[0])
            return s->layouts[i]->report_len == report_len ? s->layouts[i] : NULL;
    }
    return NULL;
}

static int infr_route(infr_session_t *s, const unsigned char *report, size_t report_len, infr_layout_t **out)
{
    size_t i;

    *out = infr_find_layout(s, report, report_len);
    if (*out != NULL)
        return INFR_OK;
    if (!(s->flags & INFR_REPORT_IDS) || report_len > s->report_len)
        return INFR_ERR_LEN;
    if (report[0] == 0)
        return INFR_ERR_ARG;
    for (i = 0; i < s->layout_count; i++) {
        if (s->layouts[i]->report_id == report[0])
            return INFR_ERR_LEN;
    }
    return infr_add_layout(s, report[0], report_len, out);
}

static int infr_label_known(const infr_session_t *s, int label)
{
    size_t i;

    if (label <= INFR_REST)
        return 1;
    for (i = 0; i < s->label_count; i++) {
        if (s->seen_labels[i] == label)
            return 1;
    }
    return 0;
}

int infr_add(infr_session_t *session, int label, const unsigned char *report, size_t report_len)
{
    infr_layout_t *layout;
    int status;

    if (session == NULL || report == NULL || label < INFR_FREE || report_len == 0)
        return INFR_ERR_ARG;
    if (!infr_label_known(session, label) && session->label_count == INFR_MAX_LABELS)
        return INFR_ERR_FULL;
    status = infr_route(session, report, report_len, &layout);
    if (status != INFR_OK)
        return status;
    if (layout->count == layout->max_samples)
        return INFR_ERR_FULL;
    if (!infr_label_known(session, label))
        session->seen_labels[session->label_count++] = label;
    return infr_layout_add(layout, label, report);
}

size_t infr_report_count(const infr_session_t *session)
{
    return session != NULL ? session->layout_count : 0;
}

int infr_report_info(const infr_session_t *session, size_t index, unsigned *out_report_id,
                     size_t *out_report_len, size_t *out_samples)
{
    const infr_layout_t *l;

    if (session == NULL || index >= session->layout_count)
        return INFR_ERR_ARG;
    l = session->layouts[index];
    if (out_report_id != NULL)
        *out_report_id = l->report_id;
    if (out_report_len != NULL)
        *out_report_len = l->report_len;
    if (out_samples != NULL)
        *out_samples = l->count;
    return INFR_OK;
}
