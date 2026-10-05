#include "infer_int.h"

#include <string.h>

static const uint32_t infr_axis_usages[] = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};

static int64_t infr_abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static int infr_prefers_signed(const infr_layout_t *s, infr_field_t *f)
{
    int64_t as_unsigned = 0;
    int64_t as_signed = 0;
    size_t i;

    for (i = 1; i < s->count; i++) {
        int32_t a;
        int32_t b;

        f->flags &= ~INFR_SIGNED;
        a = infr_value(f, infr_sample(s, i - 1));
        b = infr_value(f, infr_sample(s, i));
        as_unsigned += infr_abs64((int64_t)b - a);
        f->flags |= INFR_SIGNED;
        a = infr_value(f, infr_sample(s, i - 1));
        b = infr_value(f, infr_sample(s, i));
        as_signed += infr_abs64((int64_t)b - a);
    }
    f->flags &= ~INFR_SIGNED;
    return as_signed * 2 < as_unsigned;
}

static int32_t infr_rest_value(const infr_layout_t *s, const infr_field_t *f)
{
    int64_t sum = 0;
    size_t n = 0;
    size_t i;

    for (i = 0; i < s->count; i++) {
        if (s->labels[i] != INFR_REST)
            continue;
        sum += infr_value(f, infr_sample(s, i));
        n++;
    }
    if (n == 0)
        return infr_value(f, infr_sample(s, 0));
    return (int32_t)((sum + (sum < 0 ? -(int64_t)n / 2 : (int64_t)n / 2)) / (int64_t)n);
}

static void infr_measure(const infr_layout_t *s, infr_field_t *f)
{
    size_t i;

    f->rest = infr_rest_value(s, f);
    f->logical_min = f->logical_max = infr_value(f, infr_sample(s, 0));
    f->noise = 0;
    for (i = 0; i < s->count; i++) {
        int32_t v = infr_value(f, infr_sample(s, i));
        int64_t d = infr_abs64((int64_t)v - f->rest);

        f->logical_min = v < f->logical_min ? v : f->logical_min;
        f->logical_max = v > f->logical_max ? v : f->logical_max;
        if (s->labels[i] == INFR_REST && d > f->noise)
            f->noise = d > INT32_MAX ? INT32_MAX : (int32_t)d;
    }
    if (f->noise > 0)
        f->flags |= INFR_NOISY;
}

static void infr_describe_bit(infr_field_t *f)
{
    if (f->rest == 1 && f->kind == INFR_BUTTON) {
        f->flags |= INFR_ACTIVE_LOW;
        f->rest = 0;
        f->logical_min = 0;
        f->logical_max = 1;
    }
}

static void infr_describe_hat(const infr_layout_t *s, infr_field_t *f)
{
    f->rest = (int32_t)infr_rest_raw(s, f->bit_offset, f->bit_size);
    f->flags |= INFR_NULL_STATE;
    f->flags &= ~INFR_NOISY;
    f->noise = 0;
    f->logical_min = f->rest == 0 ? 1 : 0;
    f->logical_max = f->rest == 0 ? 8 : 7;
}

void infr_describe_field(infr_layout_t *s, infr_field_t *f)
{
    if (f->kind == INFR_AXIS && (f->bit_size >= 32 || infr_prefers_signed(s, f)))
        f->flags |= INFR_SIGNED;
    f->label = infr_field_label(s, f);
    infr_measure(s, f);
    if (f->kind == INFR_BUTTON)
        infr_describe_bit(f);
    else if (f->kind == INFR_HAT)
        infr_describe_hat(s, f);
}

void infr_add_constants(infr_layout_t *s)
{
    size_t b = 0;

    while (b < s->bits) {
        size_t start = b;
        infr_field_t *f;

        if (s->bit_owner[b] != INFR_OWNER_FREE) {
            b++;
            continue;
        }
        while (b < s->bits && s->bit_owner[b] == INFR_OWNER_FREE && (b == start || b % 8 != 0))
            b++;
        f = infr_new_field(s, start, (unsigned)(b - start), INFR_CONSTANT);
        f->rest = f->logical_min = f->logical_max = infr_value(f, infr_sample(s, 0));
    }
}

void infr_sort_fields(infr_layout_t *s)
{
    size_t i;

    for (i = 1; i < s->field_count; i++) {
        infr_field_t key = s->fields[i];
        size_t j = i;

        while (j > 0 && s->fields[j - 1].bit_offset > key.bit_offset) {
            s->fields[j] = s->fields[j - 1];
            j--;
        }
        s->fields[j] = key;
    }
}

static const infr_hint_t *infr_hint(const infr_layout_t *s, int label)
{
    size_t i;

    for (i = 0; i < s->hints->count; i++) {
        if (s->hints->items[i].label == label)
            return &s->hints->items[i];
    }
    return NULL;
}

static int infr_hint_taken(const infr_layout_t *s, uint32_t page, uint32_t usage)
{
    size_t i;

    for (i = 0; i < s->hints->count; i++) {
        if (s->hints->items[i].usage_page == page && s->hints->items[i].usage == usage)
            return 1;
    }
    return 0;
}

static int infr_usage_taken(const infr_layout_t *s, uint32_t page, uint32_t usage)
{
    size_t i;

    for (i = 0; i < s->field_count; i++) {
        if (s->fields[i].usage_page == page && s->fields[i].usage == usage)
            return 1;
    }
    return infr_hint_taken(s, page, usage);
}

typedef struct {
    uint32_t button;
    uint32_t vendor;
} infr_usage_cursor_t;

static void infr_next_usage(const infr_layout_t *s, infr_field_t *f, infr_usage_cursor_t *next)
{
    size_t i;

    if (f->kind == INFR_BUTTON) {
        while (infr_hint_taken(s, 0x09, next->button))
            next->button++;
        f->usage_page = 0x09;
        f->usage = next->button++;
        return;
    }
    for (i = 0; i < sizeof(infr_axis_usages) / sizeof(infr_axis_usages[0]); i++) {
        if (!infr_usage_taken(s, 0x01, infr_axis_usages[i])) {
            f->usage_page = 0x01;
            f->usage = infr_axis_usages[i];
            return;
        }
    }
    while (infr_hint_taken(s, 0xff00, next->vendor))
        next->vendor++;
    f->usage_page = 0xff00;
    f->usage = next->vendor++;
}

void infr_assign_usages(infr_layout_t *s)
{
    infr_usage_cursor_t next = {1, 1};
    size_t i;

    for (i = 0; i < s->field_count; i++) {
        infr_field_t *f = &s->fields[i];
        const infr_hint_t *h = f->label > INFR_REST ? infr_hint(s, f->label) : NULL;

        if (f->kind != INFR_BUTTON && f->kind != INFR_AXIS && f->kind != INFR_HAT)
            continue;
        f->usage_page = 0;
        f->usage = 0;
        if (h != NULL) {
            f->usage_page = h->usage_page;
            f->usage = h->usage;
        } else if (f->kind == INFR_HAT) {
            f->usage_page = 0x01;
            f->usage = 0x39;
        }
    }
    for (i = 0; i < s->field_count; i++) {
        infr_field_t *f = &s->fields[i];

        if ((f->kind == INFR_BUTTON || f->kind == INFR_AXIS) && f->usage_page == 0)
            infr_next_usage(s, f, &next);
    }
}
