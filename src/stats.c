#include "infer_int.h"

#include <string.h>

#define INFR_COUNTER_MIN 4u

const unsigned char *infr_sample(const infr_session_t *s, size_t index)
{
    return s->reports + index * s->report_len;
}

int infr_bit(const unsigned char *report, size_t bit)
{
    return report[bit >> 3] >> (bit & 7) & 1;
}

uint32_t infr_raw(const unsigned char *report, size_t bit_offset, unsigned bit_size, int big_endian)
{
    uint32_t value = 0;
    unsigned i;

    if (big_endian) {
        for (i = 0; i < bit_size / 8; i++)
            value = value << 8 | report[(bit_offset >> 3) + i];
        return value;
    }
    {
        uint64_t wide = 0;
        size_t first = bit_offset >> 3;
        unsigned shift = (unsigned)(bit_offset & 7);
        unsigned bytes = (shift + bit_size + 7) / 8;

        for (i = 0; i < bytes; i++)
            wide |= (uint64_t)report[first + i] << (8 * i);
        wide >>= shift;
        return bit_size >= 32 ? (uint32_t)wide : (uint32_t)(wide & ((1u << bit_size) - 1));
    }
}

int32_t infr_value(const infr_field_t *field, const unsigned char *report)
{
    uint32_t raw = infr_raw(report, field->bit_offset, field->bit_size,
                            (field->flags & INFR_BIG_ENDIAN) != 0);
    uint32_t sign;

    if (!(field->flags & INFR_SIGNED))
        return (int32_t)raw;
    sign = 1u << (field->bit_size - 1);
    return (int32_t)((int64_t)(raw ^ sign) - (int64_t)sign);
}

int infr_transition_label(const infr_session_t *s, size_t index)
{
    int a = s->labels[index - 1];

    return a == s->labels[index] ? a : INFR_FREE;
}

infr_field_t *infr_new_field(infr_session_t *s, size_t bit_offset, unsigned bit_size, unsigned kind)
{
    infr_field_t *f = &s->fields[s->field_count++];
    unsigned i;

    memset(f, 0, sizeof(*f));
    f->bit_offset = bit_offset;
    f->bit_size = bit_size;
    f->kind = kind;
    f->label = INFR_NO_LABEL;
    f->seed = -1;
    for (i = 0; i < bit_size; i++)
        s->bit_owner[bit_offset + i] = INFR_OWNER_TAKEN;
    return f;
}

int infr_window_free(const infr_session_t *s, size_t bit_offset, unsigned bit_size)
{
    unsigned i;

    if (bit_offset + bit_size > s->bits)
        return 0;
    for (i = 0; i < bit_size; i++) {
        if (s->bit_owner[bit_offset + i] != INFR_OWNER_FREE)
            return 0;
    }
    return 1;
}

typedef struct {
    int labels[INFR_MAX_LABELS];
    uint64_t moves[INFR_MAX_LABELS];
    uint32_t total[INFR_MAX_LABELS];
    uint64_t rest_moves;
    uint32_t rest_total;
} infr_label_stats_t;

static void infr_count_label(infr_label_stats_t *st, int label, uint32_t moved)
{
    size_t i;

    for (i = 0; i < INFR_MAX_LABELS; i++) {
        if (st->total[i] == 0 || st->labels[i] == label) {
            st->labels[i] = label;
            st->total[i]++;
            st->moves[i] += moved;
            return;
        }
    }
}

static double infr_rate(uint64_t moves, uint32_t total)
{
    return total == 0 ? 0.0 : (double)moves / (double)total;
}

static double infr_median_other(const infr_label_stats_t *st, size_t best, size_t used)
{
    double rates[INFR_MAX_LABELS + 1];
    size_t n = 0;
    size_t i;
    size_t j;

    for (i = 0; i < used; i++) {
        if (i != best)
            rates[n++] = infr_rate(st->moves[i], st->total[i]);
    }
    if (st->rest_total > 0)
        rates[n++] = infr_rate(st->rest_moves, st->rest_total);
    for (i = 1; i < n; i++) {
        double key = rates[i];

        for (j = i; j > 0 && rates[j - 1] > key; j--)
            rates[j] = rates[j - 1];
        rates[j] = key;
    }
    return n == 0 ? 0.0 : rates[n / 2];
}

static int infr_best_label(const infr_label_stats_t *st)
{
    size_t best = 0;
    size_t used = 0;
    size_t i;

    while (used < INFR_MAX_LABELS && st->total[used] > 0)
        used++;
    for (i = 1; i < used; i++) {
        if (infr_rate(st->moves[i], st->total[i]) > infr_rate(st->moves[best], st->total[best]))
            best = i;
    }
    if (used == 0 || st->moves[best] == 0)
        return INFR_NO_LABEL;
    if (infr_rate(st->moves[best], st->total[best]) <= 1.4 * infr_median_other(st, best, used))
        return INFR_NO_LABEL;
    return st->labels[best];
}

int infr_field_label(const infr_session_t *s, const infr_field_t *field)
{
    infr_label_stats_t st;
    size_t i;

    memset(&st, 0, sizeof(st));
    for (i = 1; i < s->count; i++) {
        int label = infr_transition_label(s, i);
        int64_t delta;
        uint32_t moved;

        if (label < INFR_REST)
            continue;
        delta = (int64_t)infr_value(field, infr_sample(s, i)) - infr_value(field, infr_sample(s, i - 1));
        moved = (uint32_t)(delta < 0 ? -delta : delta);
        if (label == INFR_REST) {
            st.rest_total++;
            st.rest_moves += moved;
        } else {
            infr_count_label(&st, label, moved);
        }
    }
    return infr_best_label(&st);
}

static void infr_count_transition(infr_session_t *s, size_t index)
{
    const unsigned char *prev = infr_sample(s, index - 1);
    const unsigned char *cur = infr_sample(s, index);
    int rest = infr_transition_label(s, index) == INFR_REST;
    size_t b;

    for (b = 0; b < s->bits; b++) {
        if (infr_bit(prev, b) == infr_bit(cur, b))
            continue;
        s->bit_changes[b]++;
        if (rest)
            s->bit_rest_changes[b]++;
    }
    s->rest_transitions += (size_t)rest;
}

static void infr_count_rest(infr_session_t *s, size_t index)
{
    const unsigned char *r = infr_sample(s, index);
    size_t b;

    if (s->labels[index] != INFR_REST)
        return;
    s->rest_samples++;
    for (b = 0; b < s->bits; b++)
        s->bit_rest_ones[b] += (uint32_t)infr_bit(r, b);
}

static void infr_bit_labels(infr_session_t *s)
{
    infr_field_t one;
    size_t b;

    memset(&one, 0, sizeof(one));
    one.bit_size = 1;
    for (b = 0; b < s->bits; b++) {
        one.bit_offset = b;
        s->bit_label[b] = s->bit_changes[b] ? infr_field_label(s, &one) : INFR_NO_LABEL;
    }
}

void infr_collect_stats(infr_session_t *s)
{
    size_t i;
    size_t bytes = s->bits * sizeof(uint32_t);

    memset(s->bit_changes, 0, bytes);
    memset(s->bit_rest_changes, 0, bytes);
    memset(s->bit_rest_ones, 0, bytes);
    s->rest_samples = 0;
    s->rest_transitions = 0;
    for (i = 0; i < s->count; i++) {
        infr_count_rest(s, i);
        if (i > 0)
            infr_count_transition(s, i);
    }
    infr_bit_labels(s);
}

static uint32_t infr_gcd(uint32_t a, uint32_t b)
{
    while (b != 0) {
        uint32_t t = a % b;

        a = b;
        b = t;
    }
    return a;
}

typedef struct {
    size_t changed;
    size_t forward;
    size_t step_hits;
    uint32_t step;
    uint32_t gcd;
    size_t top_changes;
    size_t top_carries;
} infr_counter_stats_t;

static void infr_counter_step(infr_counter_stats_t *c, uint32_t prev, uint32_t cur, unsigned w)
{
    uint32_t mask = w >= 32 ? 0xffffffffu : (1u << w) - 1;
    uint32_t low = mask >> 1;
    uint32_t delta = (cur - prev) & mask;

    if (delta == 0)
        return;
    c->changed++;
    if (((cur ^ prev) >> (w - 1)) & 1) {
        c->top_changes++;
        c->top_carries += (cur & low) < (prev & low);
    }
    if (delta > low)
        return;
    c->forward++;
    c->gcd = infr_gcd(c->gcd, delta);
    if (c->step_hits == 0)
        c->step = delta;
    c->step_hits += delta == c->step;
}

static int infr_counter_carries(const infr_session_t *s, size_t off, unsigned w, unsigned first)
{
    size_t toggles[32];
    size_t carries[32];
    size_t i;
    unsigned k;

    memset(toggles, 0, sizeof(toggles));
    memset(carries, 0, sizeof(carries));
    for (i = 1; i < s->count; i++) {
        uint32_t prev = infr_raw(infr_sample(s, i - 1), off, w, 0);
        uint32_t cur = infr_raw(infr_sample(s, i), off, w, 0);

        for (k = first; k < w; k++) {
            uint32_t low = (1u << k) - 1;

            if (((cur ^ prev) >> k) & 1) {
                toggles[k]++;
                carries[k] += (cur & low) < (prev & low);
            }
        }
    }
    for (k = first; k < w; k++) {
        if (carries[k] * 10 < toggles[k] * 9)
            return 0;
    }
    return 1;
}

static unsigned infr_bits_for(uint32_t v)
{
    unsigned n = 0;

    while (n < 32 && (v >> n) != 0)
        n++;
    return n;
}

static int infr_is_counter(const infr_session_t *s, size_t off, unsigned w, int32_t *out_step)
{
    infr_counter_stats_t c;
    size_t transitions = s->count - 1;
    size_t i;

    memset(&c, 0, sizeof(c));
    for (i = 1; i < s->count; i++)
        infr_counter_step(&c, infr_raw(infr_sample(s, i - 1), off, w, 0),
                          infr_raw(infr_sample(s, i), off, w, 0), w);
    if (c.changed * 10 < transitions * 9 || c.forward * 20 < c.changed * 19 || c.gcd != 1)
        return 0;
    if (c.top_changes == 0 || c.top_carries * 10 < c.top_changes * 9)
        return 0;
    *out_step = c.step_hits * 10 >= c.forward * 9 ? (int32_t)c.step : 0;
    return infr_counter_carries(s, off, w, *out_step > 0 ? infr_bits_for((uint32_t)*out_step * 2) : 8);
}

static unsigned infr_counter_at(const infr_session_t *s, size_t off, int32_t *out_step)
{
    unsigned w;
    unsigned max = s->bits - off < 32 ? (unsigned)(s->bits - off) : 32;

    if (s->bit_changes[off] * 4 < s->count - 1)
        return 0;
    for (w = max; w >= INFR_COUNTER_MIN; w--) {
        if (!infr_window_free(s, off, w) || s->bit_changes[off + w - 1] == 0)
            continue;
        if (infr_is_counter(s, off, w, out_step))
            return w;
    }
    return 0;
}

void infr_find_counters(infr_session_t *s)
{
    size_t off;

    for (off = 0; off + INFR_COUNTER_MIN <= s->bits; off++) {
        int32_t step = 0;
        infr_field_t *f;
        unsigned w;

        if (s->bit_owner[off] != INFR_OWNER_FREE)
            continue;
        w = infr_counter_at(s, off, &step);
        if (w == 0)
            continue;
        f = infr_new_field(s, off, w, INFR_COUNTER);
        f->step = step;
        f->logical_max = w >= 32 ? INT32_MAX : (int32_t)((1u << (w - 1)) * 2 - 1);
        off += w - 1;
    }
}

static int infr_rest_bit(const infr_session_t *s, size_t bit)
{
    if (s->rest_samples > 0)
        return s->bit_rest_ones[bit] * 2 > s->rest_samples;
    return infr_bit(infr_sample(s, 0), bit);
}

uint32_t infr_rest_raw(const infr_session_t *s, size_t off, unsigned w)
{
    uint32_t v = 0;
    unsigned i;

    for (i = 0; i < w; i++)
        v |= (uint32_t)infr_rest_bit(s, off + i) << i;
    return v;
}

int infr_noise_bit(const infr_session_t *s, size_t bit)
{
    size_t total = s->count - 1;

    if (s->rest_transitions == 0 || s->bit_rest_changes[bit] == 0)
        return 0;
    return (uint64_t)s->bit_rest_changes[bit] * total * 2 >= (uint64_t)s->bit_changes[bit] * s->rest_transitions;
}
