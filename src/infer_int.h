#ifndef INFER_INT_H
#define INFER_INT_H

#include "infer/infer.h"

#if defined(INFR_IMPLEMENTATION_DONE)
#define INFR_INTERNAL static
#else
#define INFR_INTERNAL
#endif

#define INFR_OWNER_FREE 0
#define INFR_OWNER_TAKEN 1

typedef struct {
    int label;
    uint32_t usage_page;
    uint32_t usage;
} infr_hint_t;

struct infr_session {
    infr_alloc_fn alloc_fn;
    infr_free_fn free_fn;
    void *alloc_ctx;
    size_t report_len;
    size_t bits;
    size_t max_samples;
    size_t count;
    unsigned char *reports;
    int *labels;
    infr_field_t *fields;
    size_t field_count;
    uint32_t *bit_changes;
    uint32_t *bit_rest_changes;
    uint32_t *bit_rest_ones;
    int *bit_label;
    unsigned char *bit_owner;
    size_t rest_samples;
    size_t rest_transitions;
    infr_hint_t hints[INFR_MAX_HINTS];
    int seen_labels[INFR_MAX_LABELS];
    size_t label_count;
    size_t hint_count;
    int analyzed;
};

INFR_INTERNAL const unsigned char *infr_sample(const infr_session_t *s, size_t index);
INFR_INTERNAL int infr_bit(const unsigned char *report, size_t bit);
INFR_INTERNAL uint32_t infr_raw(const unsigned char *report, size_t bit_offset, unsigned bit_size,
                                int big_endian);
INFR_INTERNAL int32_t infr_value(const infr_field_t *field, const unsigned char *report);
INFR_INTERNAL int infr_transition_label(const infr_session_t *s, size_t index);
INFR_INTERNAL infr_field_t *infr_new_field(infr_session_t *s, size_t bit_offset, unsigned bit_size,
                                           unsigned kind);
INFR_INTERNAL int infr_window_free(const infr_session_t *s, size_t bit_offset, unsigned bit_size);
INFR_INTERNAL uint32_t infr_rest_raw(const infr_session_t *s, size_t off, unsigned w);
INFR_INTERNAL int infr_noise_bit(const infr_session_t *s, size_t bit);
INFR_INTERNAL int infr_field_label(const infr_session_t *s, const infr_field_t *field);

INFR_INTERNAL void infr_collect_stats(infr_session_t *s);
INFR_INTERNAL void infr_find_counters(infr_session_t *s);
INFR_INTERNAL void infr_find_checksums(infr_session_t *s);
INFR_INTERNAL void infr_find_big_endian(infr_session_t *s);
INFR_INTERNAL void infr_find_hats(infr_session_t *s);
INFR_INTERNAL void infr_find_chains(infr_session_t *s);
INFR_INTERNAL void infr_describe_field(infr_session_t *s, infr_field_t *field);
INFR_INTERNAL void infr_add_constants(infr_session_t *s);
INFR_INTERNAL void infr_sort_fields(infr_session_t *s);
INFR_INTERNAL void infr_assign_usages(infr_session_t *s);

#endif
