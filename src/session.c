#include "infer_int.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void *infr_take(const infr_alloc_t *a, size_t count, size_t size, int *failed)
{
    void *p;

    if (*failed || count > SIZE_MAX / size) {
        *failed = 1;
        return NULL;
    }
    p = a->alloc_fn(a->alloc_ctx, count * size);
    if (p == NULL)
        *failed = 1;
    else
        memset(p, 0, count * size);
    return p;
}

static void infr_release(const infr_alloc_t *a, void *p)
{
    if (p != NULL)
        a->free_fn(a->alloc_ctx, p);
}

static int infr_alloc_arrays(infr_layout_t *s)
{
    int failed = 0;

    s->reports = infr_take(&s->alloc, s->max_samples, s->report_len, &failed);
    s->labels = infr_take(&s->alloc, s->max_samples, sizeof(int), &failed);
    s->fields = infr_take(&s->alloc, s->bits, sizeof(infr_field_t), &failed);
    s->bit_changes = infr_take(&s->alloc, s->bits, sizeof(uint32_t), &failed);
    s->bit_rest_changes = infr_take(&s->alloc, s->bits, sizeof(uint32_t), &failed);
    s->bit_rest_ones = infr_take(&s->alloc, s->bits, sizeof(uint32_t), &failed);
    s->bit_label = infr_take(&s->alloc, s->bits, sizeof(int), &failed);
    s->bit_owner = infr_take(&s->alloc, s->bits, 1, &failed);
    return failed ? INFR_ERR_NOMEM : INFR_OK;
}

int infr_layout_create(const infr_alloc_t *alloc, size_t report_len, size_t max_samples,
                       const infr_hints_t *hints, infr_layout_t **out_layout)
{
    infr_layout_t *s = alloc->alloc_fn(alloc->alloc_ctx, sizeof(*s));

    *out_layout = NULL;
    if (s == NULL)
        return INFR_ERR_NOMEM;
    memset(s, 0, sizeof(*s));
    s->alloc = *alloc;
    s->report_len = report_len;
    s->bits = report_len * 8;
    s->max_samples = max_samples;
    s->hints = hints;
    if (infr_alloc_arrays(s) != INFR_OK) {
        infr_layout_destroy(s);
        return INFR_ERR_NOMEM;
    }
    *out_layout = s;
    return INFR_OK;
}

void infr_layout_destroy(infr_layout_t *s)
{
    infr_alloc_t a;

    if (s == NULL)
        return;
    a = s->alloc;
    infr_release(&a, s->reports);
    infr_release(&a, s->labels);
    infr_release(&a, s->fields);
    infr_release(&a, s->bit_changes);
    infr_release(&a, s->bit_rest_changes);
    infr_release(&a, s->bit_rest_ones);
    infr_release(&a, s->bit_label);
    infr_release(&a, s->bit_owner);
    a.free_fn(a.alloc_ctx, s);
}

int infr_layout_add(infr_layout_t *s, int label, const unsigned char *report)
{
    if (s->count == s->max_samples)
        return INFR_ERR_FULL;
    memcpy(s->reports + s->count * s->report_len, report, s->report_len);
    s->labels[s->count++] = label;
    s->analyzed = 0;
    return INFR_OK;
}

int infr_layout_analyze(infr_layout_t *s)
{
    size_t i;

    if (s->count < 2)
        return INFR_ERR_SAMPLES;
    s->field_count = 0;
    memset(s->bit_owner, 0, s->bits);
    infr_collect_stats(s);
    infr_find_counters(s);
    infr_find_checksums(s);
    infr_find_big_endian(s);
    infr_find_hats(s);
    infr_find_chains(s);
    infr_add_constants(s);
    infr_sort_fields(s);
    infr_assign_usages(s);
    for (i = 0; i < s->field_count; i++)
        s->fields[i].report_id = s->report_id;
    s->analyzed = 1;
    return INFR_OK;
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

void infr_layout_normalize(const infr_layout_t *s, unsigned char *out)
{
    size_t i;

    for (i = 0; i < s->field_count; i++) {
        const infr_field_t *f = &s->fields[i];

        if (f->kind == INFR_BUTTON && (f->flags & INFR_ACTIVE_LOW))
            out[f->bit_offset >> 3] ^= (unsigned char)(1u << (f->bit_offset & 7));
        if (f->flags & INFR_BIG_ENDIAN)
            infr_swap_bytes(out + (f->bit_offset >> 3), f->bit_size / 8);
    }
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
