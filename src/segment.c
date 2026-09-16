#include "infer_int.h"

#include <string.h>

#define INFR_HAT_VALUES 16

static int infr_labels_compatible(int a, int b)
{
    return a == b || a == INFR_NO_LABEL || b == INFR_NO_LABEL;
}

static uint32_t infr_absdiff(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

static size_t infr_wrap_ratio(const infr_session_t *s, size_t trigger_bit, size_t off, unsigned w,
                              int big_endian, size_t *out_total)
{
    size_t i;
    size_t wraps = 0;
    size_t total = 0;
    uint32_t half = 1u << (w - 1);

    for (i = 1; i < s->count; i++) {
        const unsigned char *a = infr_sample(s, i - 1);
        const unsigned char *b = infr_sample(s, i);

        if (infr_bit(a, trigger_bit) == infr_bit(b, trigger_bit))
            continue;
        total++;
        wraps += infr_absdiff(infr_raw(a, off, w, big_endian), infr_raw(b, off, w, big_endian)) >= half;
    }
    *out_total = total;
    return wraps;
}

static int infr_byte_free(const infr_session_t *s, size_t byte)
{
    return infr_window_free(s, byte * 8, 8);
}

static int infr_is_big_endian_pair(const infr_session_t *s, size_t hi)
{
    size_t total = 0;
    size_t be_wraps;
    size_t le_wraps;
    size_t le_total = 0;

    if (s->bit_changes[hi * 8] == 0 || s->bit_changes[(hi + 1) * 8 + 7] == 0)
        return 0;
    if (!infr_labels_compatible(s->bit_label[hi * 8], s->bit_label[(hi + 1) * 8]))
        return 0;
    be_wraps = infr_wrap_ratio(s, hi * 8, (hi + 1) * 8, 8, 0, &total);
    le_wraps = infr_wrap_ratio(s, (hi + 1) * 8, hi * 8, 8, 0, &le_total);
    if (total < 2 || be_wraps * 10 < total * 6)
        return 0;
    return le_total == 0 || le_wraps * 10 < le_total * 3;
}

void infr_find_big_endian(infr_session_t *s)
{
    size_t hi;

    for (hi = 0; hi + 1 < s->report_len; hi++) {
        infr_field_t *f;

        if (!infr_byte_free(s, hi) || !infr_byte_free(s, hi + 1) || !infr_is_big_endian_pair(s, hi))
            continue;
        f = infr_new_field(s, hi * 8, 16, INFR_AXIS);
        f->flags |= INFR_BIG_ENDIAN;
        infr_describe_field(s, f);
        hi++;
    }
}

static int infr_hat_shape(const unsigned char *seen, uint32_t rest)
{
    unsigned v;
    unsigned directions = 0;

    if (rest == 8 || rest == 15) {
        for (v = 8; v < INFR_HAT_VALUES; v++) {
            if (seen[v] && v != rest)
                return 0;
        }
        for (v = 0; v < 8; v++)
            directions += seen[v];
        return directions >= 3;
    }
    if (rest != 0)
        return 0;
    for (v = 9; v < INFR_HAT_VALUES; v++) {
        if (seen[v])
            return 0;
    }
    for (v = 1; v <= 8; v++)
        directions += seen[v];
    return directions >= 5;
}

static int infr_is_hat(const infr_session_t *s, size_t off)
{
    unsigned char seen[INFR_HAT_VALUES];
    unsigned varying = 0;
    unsigned i;

    for (i = 0; i < 4; i++) {
        varying += s->bit_changes[off + i] > 0;
        if (i > 0 && !infr_labels_compatible(s->bit_label[off], s->bit_label[off + i]))
            return 0;
    }
    if (varying < 2)
        return 0;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < s->count; i++)
        seen[infr_raw(infr_sample(s, i), off, 4, 0)] = 1;
    return infr_hat_shape(seen, infr_rest_raw(s, off, 4));
}

void infr_find_hats(infr_session_t *s)
{
    size_t off;

    for (off = 0; off + 4 <= s->bits; off += 4) {
        infr_field_t *f;

        if (!infr_window_free(s, off, 4) || !infr_is_hat(s, off))
            continue;
        f = infr_new_field(s, off, 4, INFR_HAT);
        infr_describe_field(s, f);
    }
}

static int infr_top_smooth(const infr_session_t *s, size_t off, unsigned w)
{
    size_t top = off + w - 1;
    size_t allowed = s->bit_changes[top] / 10;
    size_t fails = 0;
    uint32_t mask = w >= 32 ? 0xffffffffu : (1u << w) - 1;
    uint32_t far = 1u << (w - 2);
    unsigned low = w - 1 < 3 ? w - 1 : 3;
    size_t i;

    for (i = 1; i < s->count; i++) {
        const unsigned char *a = infr_sample(s, i - 1);
        const unsigned char *b = infr_sample(s, i);
        uint32_t d;

        if (infr_bit(a, top) == infr_bit(b, top))
            continue;
        d = (infr_raw(b, off, w, 0) - infr_raw(a, off, w, 0)) & mask;
        if ((d >= far && mask - d + 1 >= far) || infr_raw(a, off, low, 0) == infr_raw(b, off, low, 0)) {
            if (++fails > allowed)
                return 0;
        }
    }
    return 1;
}

static unsigned infr_free_run(const infr_session_t *s, size_t off)
{
    unsigned w = 0;

    while (w < 32 && off + w < s->bits && s->bit_owner[off + w] == INFR_OWNER_FREE &&
           s->bit_changes[off + w] > 0 && (w == 0 || s->bit_changes[off + w - 1] * 3 + 4 >= s->bit_changes[off + w]))
        w++;
    return w;
}

static int infr_carries(const infr_session_t *s, size_t off, unsigned k)
{
    size_t toggles = 0;
    size_t carried = 0;
    size_t i;

    for (i = 1; i < s->count; i++) {
        const unsigned char *a = infr_sample(s, i - 1);
        const unsigned char *b = infr_sample(s, i);

        if (infr_bit(a, off + k) == infr_bit(b, off + k))
            continue;
        toggles++;
        carried += infr_raw(a, off, 3, 0) != infr_raw(b, off, 3, 0);
    }
    return carried * 10 >= toggles * 8;
}

static int infr_chain_ok(const infr_session_t *s, size_t off, unsigned w)
{
    unsigned k;

    if (!infr_labels_compatible(s->bit_label[off], s->bit_label[off + w - 1]) || !infr_top_smooth(s, off, w))
        return 0;
    for (k = 3; k < w; k++) {
        if (!infr_carries(s, off, k))
            return 0;
    }
    return 1;
}

static unsigned infr_chain_width(const infr_session_t *s, size_t off)
{
    unsigned w;

    for (w = infr_free_run(s, off); w >= 3; w--) {
        if (infr_chain_ok(s, off, w))
            return w;
    }
    return 1;
}

void infr_find_chains(infr_session_t *s)
{
    size_t off;

    for (off = 0; off < s->bits; off++) {
        unsigned w;
        infr_field_t *f;

        if (s->bit_owner[off] != INFR_OWNER_FREE || s->bit_changes[off] == 0)
            continue;
        w = infr_chain_width(s, off);
        if (w == 1 && infr_noise_bit(s, off)) {
            while (w < 32 && off + w < s->bits && s->bit_owner[off + w] == INFR_OWNER_FREE &&
                   infr_noise_bit(s, off + w))
                w++;
            f = infr_new_field(s, off, w, INFR_NOISE);
            infr_describe_field(s, f);
            off += w - 1;
            continue;
        }
        f = infr_new_field(s, off, w, w == 1 ? INFR_BUTTON : INFR_AXIS);
        infr_describe_field(s, f);
        off += w - 1;
    }
}
