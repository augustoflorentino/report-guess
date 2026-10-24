#ifndef INFER_H
#define INFER_H

#include <stddef.h>
#include <stdint.h>

#if defined(INFR_STATIC) && defined(__GNUC__)
#define INFR_API static __attribute__((unused))
#elif defined(INFR_STATIC)
#define INFR_API static
#elif defined(_WIN32) && defined(INFR_BUILD_SHARED)
#define INFR_API __declspec(dllexport)
#elif defined(_WIN32) && defined(INFR_SHARED)
#define INFR_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(INFR_BUILD_SHARED)
#define INFR_API __attribute__((visibility("default")))
#else
#define INFR_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define INFR_OK 0
#define INFR_ERR_ARG 1
#define INFR_ERR_NOMEM 2
#define INFR_ERR_LEN 3
#define INFR_ERR_FULL 4
#define INFR_ERR_SAMPLES 5
#define INFR_ERR_STATE 6
#define INFR_ERR_SPACE 7

#define INFR_REST 0
#define INFR_FREE (-1)
#define INFR_NO_LABEL (-2)

#define INFR_CONSTANT 0u
#define INFR_BUTTON 1u
#define INFR_AXIS 2u
#define INFR_HAT 3u
#define INFR_COUNTER 4u
#define INFR_CHECKSUM 5u
#define INFR_NOISE 6u

#define INFR_SIGNED 0x01u
#define INFR_BIG_ENDIAN 0x02u
#define INFR_ACTIVE_LOW 0x04u
#define INFR_NOISY 0x08u
#define INFR_NULL_STATE 0x10u

#define INFR_SUM8 1u
#define INFR_SUM8_NEG 2u
#define INFR_XOR8 3u
#define INFR_CRC8 4u
#define INFR_CRC8_MAXIM 5u
#define INFR_CRC32 6u

#define INFR_MAX_HINTS 64
#define INFR_MAX_LABELS 64
#define INFR_MAX_REPORTS 16

#define INFR_REPORT_IDS 0x1u

typedef struct infr_session infr_session_t;
typedef struct infr_model infr_model_t;

typedef void *(*infr_alloc_fn)(void *ctx, size_t size);
typedef void (*infr_free_fn)(void *ctx, void *ptr);

typedef struct infr_options {
    unsigned flags;
    infr_alloc_fn alloc_fn;
    infr_free_fn free_fn;
    void *alloc_ctx;
} infr_options_t;

typedef struct infr_field {
    unsigned report_id;
    size_t bit_offset;
    unsigned bit_size;
    unsigned kind;
    unsigned flags;
    int32_t logical_min;
    int32_t logical_max;
    int32_t rest;
    int32_t noise;
    int label;
    uint32_t usage_page;
    uint32_t usage;
    unsigned algorithm;
    int32_t step;
    size_t covered_len;
    int seed;
} infr_field_t;

INFR_API int infr_create(size_t report_len, size_t max_samples, const infr_options_t *options,
                         infr_session_t **out_session);
INFR_API void infr_destroy(infr_session_t *session);
INFR_API int infr_add(infr_session_t *session, int label, const unsigned char *report,
                      size_t report_len);
INFR_API size_t infr_report_count(const infr_session_t *session);
INFR_API int infr_report_info(const infr_session_t *session, size_t index, unsigned *out_report_id,
                              size_t *out_report_len, size_t *out_samples);
INFR_API int infr_analyze(infr_session_t *session, infr_model_t **out_model);

INFR_API void infr_model_destroy(infr_model_t *model);
INFR_API int infr_set_usage(infr_model_t *model, int label, uint32_t usage_page, uint32_t usage);
INFR_API size_t infr_field_count(const infr_model_t *model);
INFR_API const infr_field_t *infr_field(const infr_model_t *model, size_t index);
INFR_API int infr_normalize(const infr_model_t *model, const unsigned char *report,
                            size_t report_len, unsigned char *out, size_t out_len);
INFR_API int infr_descriptor(const infr_model_t *model, unsigned char *out, size_t out_len,
                             size_t *out_written);
INFR_API size_t infr_model_report_count(const infr_model_t *model);
INFR_API int infr_model_report_info(const infr_model_t *model, size_t index, unsigned *out_report_id,
                                    size_t *out_report_len);
INFR_API int infr_read(const infr_model_t *model, const infr_field_t *field,
                       const unsigned char *report, size_t report_len, int64_t *out_value);
INFR_API const char *infr_strerror(int status);

#ifdef __cplusplus
}
#endif

#endif
