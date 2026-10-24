#include "infer_int.h"

#include <string.h>

void infr_model_destroy(infr_model_t *model)
{
    size_t i;

    if (model == NULL)
        return;
    for (i = 0; i < model->layout_count; i++)
        infr_layout_destroy(model->layouts[i]);
    model->alloc.free_fn(model->alloc.alloc_ctx, model);
}

static int infr_model_take(infr_model_t *m, const infr_layout_t *src)
{
    int status = infr_layout_copy(&m->alloc, src, &m->hints, &m->layouts[m->layout_count]);

    if (status != INFR_OK)
        return status;
    infr_assign_usages(m->layouts[m->layout_count++]);
    return INFR_OK;
}

static int infr_model_new(const infr_session_t *s, infr_model_t **out_model)
{
    infr_model_t *m = s->alloc.alloc_fn(s->alloc.alloc_ctx, sizeof(*m));

    if (m == NULL)
        return INFR_ERR_NOMEM;
    memset(m, 0, sizeof(*m));
    m->alloc = s->alloc;
    m->flags = s->flags;
    *out_model = m;
    return INFR_OK;
}

int infr_analyze(infr_session_t *session, infr_model_t **out_model)
{
    infr_model_t *m = NULL;
    size_t i;
    int status;

    if (out_model == NULL)
        return INFR_ERR_ARG;
    *out_model = NULL;
    if (session == NULL)
        return INFR_ERR_ARG;
    status = infr_model_new(session, &m);
    for (i = 0; status == INFR_OK && i < session->layout_count; i++) {
        infr_layout_t *l = session->layouts[i];

        if (infr_layout_analyze(l) == INFR_OK)
            status = infr_model_take(m, l);
    }
    if (status == INFR_OK && m->layout_count == 0)
        status = INFR_ERR_SAMPLES;
    if (status != INFR_OK) {
        infr_model_destroy(m);
        return status;
    }
    *out_model = m;
    return INFR_OK;
}

int infr_set_usage(infr_model_t *model, int label, uint32_t usage_page, uint32_t usage)
{
    infr_hints_t *h;
    size_t i;

    if (model == NULL || label <= INFR_REST || usage_page == 0 || usage_page > 0xffff || usage > 0xffff)
        return INFR_ERR_ARG;
    h = &model->hints;
    for (i = 0; i < h->count && h->items[i].label != label; i++)
        ;
    if (i == INFR_MAX_HINTS)
        return INFR_ERR_FULL;
    h->items[i].label = label;
    h->items[i].usage_page = usage_page;
    h->items[i].usage = usage;
    if (i == h->count)
        h->count++;
    for (i = 0; i < model->layout_count; i++)
        infr_assign_usages(model->layouts[i]);
    return INFR_OK;
}

size_t infr_field_count(const infr_model_t *model)
{
    size_t n = 0;
    size_t i;

    for (i = 0; model != NULL && i < model->layout_count; i++)
        n += model->layouts[i]->field_count;
    return n;
}

const infr_field_t *infr_field(const infr_model_t *model, size_t index)
{
    size_t i;

    for (i = 0; model != NULL && i < model->layout_count; i++) {
        const infr_layout_t *l = model->layouts[i];

        if (index < l->field_count)
            return &l->fields[index];
        index -= l->field_count;
    }
    return NULL;
}

static const infr_layout_t *infr_model_find(const infr_model_t *m, const unsigned char *report, size_t report_len)
{
    size_t i;

    for (i = 0; i < m->layout_count; i++) {
        const infr_layout_t *l = m->layouts[i];

        if (l->report_len == report_len && (!(m->flags & INFR_REPORT_IDS) || l->report_id == report[0]))
            return l;
    }
    return NULL;
}

int infr_normalize(const infr_model_t *model, const unsigned char *report, size_t report_len,
                   unsigned char *out, size_t out_len)
{
    const infr_layout_t *layout;

    if (model == NULL || report == NULL || out == NULL || report_len == 0)
        return INFR_ERR_ARG;
    layout = infr_model_find(model, report, report_len);
    if (layout == NULL)
        return INFR_ERR_LEN;
    if (out_len < report_len)
        return INFR_ERR_SPACE;
    memmove(out, report, report_len);
    infr_layout_normalize(layout, out);
    return INFR_OK;
}

static const infr_layout_t *infr_field_layout(const infr_model_t *m, const infr_field_t *field)
{
    size_t i;

    for (i = 0; i < m->layout_count; i++) {
        const infr_layout_t *l = m->layouts[i];

        if (field >= l->fields && field < l->fields + l->field_count)
            return l;
    }
    return NULL;
}

int infr_read(const infr_model_t *model, const infr_field_t *field, const unsigned char *report,
              size_t report_len, int64_t *out_value)
{
    const infr_layout_t *layout;
    uint32_t raw;
    int64_t sign;

    if (model == NULL || field == NULL || report == NULL || out_value == NULL)
        return INFR_ERR_ARG;
    layout = infr_field_layout(model, field);
    if (layout == NULL)
        return INFR_ERR_ARG;
    if (report_len != layout->report_len || ((model->flags & INFR_REPORT_IDS) && report[0] != layout->report_id))
        return INFR_ERR_LEN;
    raw = infr_raw(report, field->bit_offset, field->bit_size, (field->flags & INFR_BIG_ENDIAN) != 0);
    if (field->flags & INFR_ACTIVE_LOW)
        raw ^= 1u;
    sign = (int64_t)1 << (field->bit_size - 1);
    *out_value = (field->flags & INFR_SIGNED) ? ((int64_t)raw ^ sign) - sign : (int64_t)raw;
    return INFR_OK;
}
