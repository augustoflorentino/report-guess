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
    status = infr_layout_create(&s->alloc, report_len, s->max_samples, &s->hints, out);
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

static int infr_track_label(infr_session_t *s, int label)
{
    size_t i;

    if (label <= INFR_REST)
        return 1;
    for (i = 0; i < s->label_count; i++) {
        if (s->seen_labels[i] == label)
            return 1;
    }
    if (s->label_count == INFR_MAX_LABELS)
        return 0;
    s->seen_labels[s->label_count++] = label;
    return 1;
}

int infr_add(infr_session_t *session, int label, const unsigned char *report, size_t report_len)
{
    infr_layout_t *layout;
    int status;

    if (session == NULL || report == NULL || label < INFR_FREE || report_len == 0)
        return INFR_ERR_ARG;
    status = infr_route(session, report, report_len, &layout);
    if (status != INFR_OK)
        return status;
    if (layout->count == layout->max_samples || !infr_track_label(session, label))
        return INFR_ERR_FULL;
    session->analyzed = 0;
    return infr_layout_add(layout, label, report);
}

int infr_set_usage(infr_session_t *session, int label, uint32_t usage_page, uint32_t usage)
{
    infr_hints_t *h;
    size_t i;

    if (session == NULL || label <= INFR_REST || usage_page == 0 || usage_page > 0xffff || usage > 0xffff)
        return INFR_ERR_ARG;
    h = &session->hints;
    for (i = 0; i < h->count && h->items[i].label != label; i++)
        ;
    if (i == INFR_MAX_HINTS)
        return INFR_ERR_FULL;
    h->items[i].label = label;
    h->items[i].usage_page = usage_page;
    h->items[i].usage = usage;
    if (i == h->count)
        h->count++;
    for (i = 0; session->analyzed && i < session->layout_count; i++)
        infr_assign_usages(session->layouts[i]);
    return INFR_OK;
}

int infr_analyze(infr_session_t *session)
{
    size_t i;

    if (session == NULL)
        return INFR_ERR_ARG;
    if (session->layout_count == 0)
        return INFR_ERR_SAMPLES;
    session->analyzed = 0;
    for (i = 0; i < session->layout_count; i++) {
        int status = infr_layout_analyze(session->layouts[i]);

        if (status != INFR_OK)
            return status;
    }
    session->analyzed = 1;
    return INFR_OK;
}

size_t infr_field_count(const infr_session_t *session)
{
    size_t n = 0;
    size_t i;

    for (i = 0; session != NULL && session->analyzed && i < session->layout_count; i++)
        n += session->layouts[i]->field_count;
    return n;
}

const infr_field_t *infr_field(const infr_session_t *session, size_t index)
{
    size_t i;

    for (i = 0; session != NULL && session->analyzed && i < session->layout_count; i++) {
        const infr_layout_t *l = session->layouts[i];

        if (index < l->field_count)
            return &l->fields[index];
        index -= l->field_count;
    }
    return NULL;
}

int infr_normalize(const infr_session_t *session, const unsigned char *report, size_t report_len,
                   unsigned char *out, size_t out_len)
{
    const infr_layout_t *layout;

    if (session == NULL || report == NULL || out == NULL || report_len == 0)
        return INFR_ERR_ARG;
    if (!session->analyzed)
        return INFR_ERR_STATE;
    layout = infr_find_layout(session, report, report_len);
    if (layout == NULL)
        return INFR_ERR_LEN;
    if (out_len < report_len)
        return INFR_ERR_SPACE;
    memmove(out, report, report_len);
    infr_layout_normalize(layout, out);
    return INFR_OK;
}

static const infr_layout_t *infr_field_layout(const infr_session_t *s, const infr_field_t *field)
{
    size_t i;

    for (i = 0; i < s->layout_count; i++) {
        const infr_layout_t *l = s->layouts[i];

        if (field >= l->fields && field < l->fields + l->field_count)
            return l;
    }
    return NULL;
}

int infr_read(const infr_session_t *session, const infr_field_t *field, const unsigned char *report,
              size_t report_len, int64_t *out_value)
{
    const infr_layout_t *layout;
    uint32_t raw;
    int64_t sign;

    if (session == NULL || field == NULL || report == NULL || out_value == NULL)
        return INFR_ERR_ARG;
    if (!session->analyzed)
        return INFR_ERR_STATE;
    layout = infr_field_layout(session, field);
    if (layout == NULL)
        return INFR_ERR_ARG;
    if (report_len != layout->report_len || ((session->flags & INFR_REPORT_IDS) && report[0] != layout->report_id))
        return INFR_ERR_LEN;
    raw = infr_raw(report, field->bit_offset, field->bit_size, (field->flags & INFR_BIG_ENDIAN) != 0);
    if (field->flags & INFR_ACTIVE_LOW)
        raw ^= 1u;
    sign = (int64_t)1 << (field->bit_size - 1);
    *out_value = (field->flags & INFR_SIGNED) ? ((int64_t)raw ^ sign) - sign : (int64_t)raw;
    return INFR_OK;
}
