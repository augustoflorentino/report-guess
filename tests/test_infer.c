#include "infer/infer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                    \
        }                                                               \
    } while (0)

#define REPORT_LEN 8
#define SAMPLES 1200

typedef struct {
    int remaining;
    int live;
} failing_alloc_t;

typedef struct {
    uint32_t rng;
    unsigned counter;
    int angle;
    int tilt;
} synth_t;

static void *failing_alloc(void *ctx, size_t size)
{
    failing_alloc_t *fa = ctx;

    if (fa->remaining-- == 0)
        return NULL;
    fa->live++;
    return malloc(size);
}

static void failing_free(void *ctx, void *ptr)
{
    failing_alloc_t *fa = ctx;

    fa->live--;
    free(ptr);
}

static unsigned synth_rand(synth_t *st, unsigned n)
{
    st->rng ^= st->rng << 13;
    st->rng ^= st->rng >> 17;
    st->rng ^= st->rng << 5;
    return st->rng % n;
}

static unsigned synth_sum8(const unsigned char *r, size_t len)
{
    unsigned sum = 0;
    size_t i;

    for (i = 0; i < len; i++)
        sum += r[i];
    return sum & 0xff;
}

static int synth_report(synth_t *st, size_t i, unsigned char *r)
{
    int label = i < 200 || i >= SAMPLES - 100 ? INFR_REST : (int)(1 + (i - 200) / 250);
    unsigned depth = 2000;

    memset(r, 0, REPORT_LEN);
    r[0] = (unsigned char)st->counter++;
    r[1] = 0xff;
    if (label == 1 && (i / 10) % 2 == 0)
        r[1] &= (unsigned char)~0x01;
    if (label == 2) {
        st->angle = (st->angle + 23) % 4000;
        depth = (unsigned)(st->angle < 2000 ? st->angle : 4000 - st->angle) * 2;
    }
    r[2] = (unsigned char)(depth >> 8);
    r[3] = (unsigned char)depth;
    r[4] = label == 3 ? (unsigned char)((i / 8) % 8) : 8;
    if (label == 4)
        st->tilt += (int)synth_rand(st, 9) - 4;
    else
        st->tilt = (int)synth_rand(st, 3) - 1;
    st->tilt = st->tilt > 100 ? 100 : st->tilt < -100 ? -100 : st->tilt;
    r[5] = (unsigned char)(st->tilt & 0xff);
    r[6] = (unsigned char)(0x80 | synth_rand(st, 2));
    r[7] = (unsigned char)synth_sum8(r, 7);
    return label;
}

static infr_session_t *synth_session(void)
{
    infr_session_t *s = NULL;
    unsigned char r[REPORT_LEN];
    synth_t st = {1, 0, 0, 0};
    size_t i;

    CHECK(infr_create(REPORT_LEN, SAMPLES, NULL, &s) == INFR_OK);
    for (i = 0; i < SAMPLES; i++) {
        int label = synth_report(&st, i, r);

        CHECK(infr_add(s, label, r, REPORT_LEN) == INFR_OK);
    }
    return s;
}

static infr_model_t *analyze(infr_session_t *s)
{
    infr_model_t *m = NULL;

    CHECK(infr_analyze(s, &m) == INFR_OK && m != NULL);
    return m;
}

static const infr_field_t *find_field(const infr_model_t *m, size_t bit_offset)
{
    size_t i;

    for (i = 0; i < infr_field_count(m); i++) {
        if (infr_field(m, i)->bit_offset == bit_offset)
            return infr_field(m, i);
    }
    return NULL;
}

static void test_create(void)
{
    static const infr_options_t half = {0, failing_alloc, NULL, NULL};
    static const infr_options_t unknown = {0x80, NULL, NULL, NULL};
    infr_session_t *s = NULL;

    CHECK(infr_create(0, 10, NULL, &s) == INFR_ERR_ARG);
    CHECK(infr_create(4, 1, NULL, &s) == INFR_ERR_ARG);
    CHECK(infr_create(4, 10, &half, &s) == INFR_ERR_ARG);
    CHECK(infr_create(4, 10, &unknown, &s) == INFR_ERR_ARG);
    CHECK(infr_create(4, 10, NULL, NULL) == INFR_ERR_ARG);
    CHECK(infr_create(4, 10, NULL, &s) == INFR_OK);
    CHECK(s != NULL);
    infr_destroy(s);
    infr_destroy(NULL);
}

static void test_add(void)
{
    static const unsigned char r[4] = {1, 2, 3, 4};
    infr_session_t *s = NULL;

    CHECK(infr_create(4, 2, NULL, &s) == INFR_OK);
    CHECK(infr_add(NULL, 0, r, 4) == INFR_ERR_ARG);
    CHECK(infr_add(s, 0, NULL, 4) == INFR_ERR_ARG);
    CHECK(infr_add(s, -3, r, 4) == INFR_ERR_ARG);
    CHECK(infr_add(s, 0, r, 3) == INFR_ERR_LEN);
    CHECK(infr_add(s, INFR_FREE, r, 4) == INFR_OK);
    CHECK(infr_add(s, 7, r, 4) == INFR_OK);
    CHECK(infr_add(s, 0, r, 4) == INFR_ERR_FULL);
    infr_destroy(s);
}

static void test_analyze(void)
{
    static const unsigned char r[2] = {0, 0};
    infr_session_t *s = NULL;
    infr_model_t *m = (infr_model_t *)&m;
    infr_model_t *again;
    const infr_field_t *f;

    CHECK(infr_analyze(NULL, &m) == INFR_ERR_ARG && m == NULL);
    CHECK(infr_create(2, 4, NULL, &s) == INFR_OK);
    CHECK(infr_analyze(s, NULL) == INFR_ERR_ARG);
    CHECK(infr_add(s, 0, r, 2) == INFR_OK);
    CHECK(infr_analyze(s, &m) == INFR_ERR_SAMPLES && m == NULL);
    infr_destroy(s);

    s = synth_session();
    m = analyze(s);
    f = find_field(m, 0);
    CHECK(f != NULL && f->kind == INFR_COUNTER && f->bit_size == 8 && f->step == 1);
    f = find_field(m, 8);
    CHECK(f != NULL && f->kind == INFR_BUTTON && (f->flags & INFR_ACTIVE_LOW) && f->label == 1);
    f = find_field(m, 16);
    CHECK(f != NULL && f->kind == INFR_AXIS && f->bit_size == 16 && (f->flags & INFR_BIG_ENDIAN));
    CHECK(f->label == 2 && f->rest == 2000 && f->logical_max <= 4000);
    f = find_field(m, 32);
    CHECK(f != NULL && f->kind == INFR_HAT && f->bit_size == 4 && f->rest == 8 && f->label == 3);
    CHECK(f->usage_page == 0x01 && f->usage == 0x39);
    f = find_field(m, 40);
    CHECK(f != NULL && f->kind == INFR_AXIS && f->bit_size == 8 && (f->flags & INFR_SIGNED));
    CHECK(f->label == 4 && (f->flags & INFR_NOISY) && f->noise == 1 && f->rest == 0);
    f = find_field(m, 48);
    CHECK(f != NULL && f->kind == INFR_NOISE && f->bit_size == 1);
    f = find_field(m, 56);
    CHECK(f != NULL && f->kind == INFR_CHECKSUM && f->algorithm == INFR_SUM8 && f->covered_len == 7);
    f = find_field(m, 0);
    infr_destroy(s);
    CHECK(find_field(m, 0) == f && f->kind == INFR_COUNTER);
    s = synth_session();
    again = analyze(s);
    CHECK(again != m && infr_field_count(again) == infr_field_count(m));
    infr_destroy(s);
    infr_model_destroy(again);
    infr_model_destroy(m);
    infr_model_destroy(NULL);
}

static void test_field(void)
{
    infr_session_t *s = synth_session();
    infr_model_t *m = analyze(s);
    size_t i;
    size_t bits = 0;

    CHECK(infr_field_count(NULL) == 0);
    CHECK(infr_field(NULL, 0) == NULL);
    for (i = 0; i < infr_field_count(m); i++) {
        CHECK(infr_field(m, i)->bit_offset == bits);
        bits += infr_field(m, i)->bit_size;
    }
    CHECK(bits == REPORT_LEN * 8);
    CHECK(infr_field(m, infr_field_count(m)) == NULL);
    infr_model_destroy(m);
    infr_destroy(s);
}

static void test_set_usage(void)
{
    infr_session_t *s = synth_session();
    infr_model_t *m = analyze(s);
    const infr_field_t *f;

    infr_destroy(s);
    CHECK(find_field(m, 8)->usage_page == 0x09 && find_field(m, 8)->usage == 1);
    CHECK(infr_set_usage(NULL, 1, 9, 1) == INFR_ERR_ARG);
    CHECK(infr_set_usage(m, 0, 9, 1) == INFR_ERR_ARG);
    CHECK(infr_set_usage(m, 1, 0x10000, 1) == INFR_ERR_ARG);
    CHECK(infr_set_usage(m, 1, 0x09, 5) == INFR_OK);
    CHECK(infr_set_usage(m, 4, 0x01, 0x40) == INFR_OK);
    CHECK(infr_set_usage(m, 4, 0x01, 0x33) == INFR_OK);
    f = find_field(m, 8);
    CHECK(f->usage_page == 0x09 && f->usage == 5);
    f = find_field(m, 40);
    CHECK(f->usage_page == 0x01 && f->usage == 0x33);
    f = find_field(m, 16);
    CHECK(f->usage_page == 0x01 && f->usage == 0x30);
    CHECK(infr_set_usage(m, 1, 0x09, 7) == INFR_OK);
    CHECK(find_field(m, 8)->usage_page == 0x09 && find_field(m, 8)->usage == 7);
    CHECK(infr_set_usage(m, 4, 0x01, 0x30) == INFR_OK);
    CHECK(find_field(m, 40)->usage == 0x30 && find_field(m, 16)->usage != 0x30);
    infr_model_destroy(m);
}

static void test_read(void)
{
    static const unsigned char r[REPORT_LEN] = {9, 0xfe, 0x12, 0x34, 3, 0xff, 0x80, 0};
    infr_session_t *ss = synth_session();
    infr_model_t *s = analyze(ss);
    infr_model_t *other = analyze(ss);
    int64_t v = 0;

    infr_destroy(ss);
    CHECK(infr_read(s, find_field(s, 8), r, REPORT_LEN, &v) == INFR_OK);
    CHECK(v == 1);
    CHECK(infr_read(s, find_field(s, 16), r, REPORT_LEN, &v) == INFR_OK && v == 0x1234);
    CHECK(infr_read(s, find_field(s, 32), r, REPORT_LEN, &v) == INFR_OK && v == 3);
    CHECK(infr_read(s, find_field(s, 40), r, REPORT_LEN, &v) == INFR_OK && v == -1);
    CHECK(infr_read(s, find_field(other, 8), r, REPORT_LEN, &v) == INFR_ERR_ARG);
    CHECK(infr_read(s, find_field(s, 8), r, REPORT_LEN - 1, &v) == INFR_ERR_LEN);
    CHECK(infr_read(s, find_field(s, 8), r, REPORT_LEN, NULL) == INFR_ERR_ARG);
    infr_model_destroy(other);
    infr_model_destroy(s);
}

static void test_normalize(void)
{
    static const unsigned char r[REPORT_LEN] = {9, 0xfe, 0x12, 0x34, 3, 0xff, 0x80, 0};
    unsigned char out[REPORT_LEN];
    infr_session_t *ss = synth_session();
    infr_model_t *s = analyze(ss);

    infr_destroy(ss);
    CHECK(infr_normalize(NULL, r, REPORT_LEN, out, REPORT_LEN) == INFR_ERR_ARG);
    CHECK(infr_normalize(s, r, REPORT_LEN - 1, out, REPORT_LEN) == INFR_ERR_LEN);
    CHECK(infr_normalize(s, r, REPORT_LEN, out, REPORT_LEN - 1) == INFR_ERR_SPACE);
    CHECK(infr_normalize(s, r, REPORT_LEN, out, REPORT_LEN) == INFR_OK);
    CHECK(out[0] == 9 && (out[1] & 1) == 1 && out[2] == 0x34 && out[3] == 0x12 && out[4] == 3);
    memcpy(out, r, REPORT_LEN);
    CHECK(infr_normalize(s, out, REPORT_LEN, out, REPORT_LEN) == INFR_OK);
    CHECK(out[2] == 0x34 && out[3] == 0x12);
    infr_model_destroy(s);
}

static void test_descriptor(void)
{
    static const unsigned char head[] = {0x05, 0x01, 0x09, 0x05, 0xa1, 0x01};
    unsigned char buf[512];
    size_t len = 0;
    size_t need = 0;
    infr_session_t *ss = synth_session();
    infr_model_t *s = analyze(ss);

    infr_destroy(ss);
    CHECK(infr_descriptor(NULL, buf, sizeof(buf), &len) == INFR_ERR_ARG);
    CHECK(infr_descriptor(s, NULL, 4, &len) == INFR_ERR_ARG);
    CHECK(infr_descriptor(s, NULL, 0, &need) == INFR_ERR_SPACE);
    CHECK(need > sizeof(head));
    CHECK(infr_descriptor(s, buf, need - 1, &len) == INFR_ERR_SPACE && len == need);
    CHECK(infr_descriptor(s, buf, sizeof(buf), &len) == INFR_OK && len == need);
    CHECK(memcmp(buf, head, sizeof(head)) == 0 && buf[len - 1] == 0xc0);
    infr_model_destroy(s);
}

static void add_two_reports(infr_session_t *s, size_t i)
{
    unsigned char a[3];
    unsigned char b[2];

    a[0] = 0x05;
    a[1] = (unsigned char)(i % 20 < 10 ? 0x01 : 0x00);
    a[2] = (unsigned char)(128 + (int)(i % 50) - 25);
    b[0] = 0x07;
    b[1] = (unsigned char)(i % 30 < 15 ? 0x02 : 0x00);
    CHECK(infr_add(s, i < 40 ? INFR_REST : INFR_FREE, a, 3) == INFR_OK);
    CHECK(infr_add(s, i < 40 ? INFR_REST : INFR_FREE, b, 2) == INFR_OK);
}

static void test_report_id(void)
{
    static const unsigned char bad_len[2] = {0x05, 0};
    static const unsigned char zero[2] = {0x00, 0};
    static const unsigned char other[2] = {0x09, 0};
    static const infr_options_t ids = {INFR_REPORT_IDS, NULL, NULL, NULL};
    unsigned char buf[256];
    unsigned char out[3];
    size_t len = 0;
    infr_session_t *s = NULL;
    infr_model_t *m = NULL;
    size_t i;

    CHECK(infr_create(3, 200, &ids, &s) == INFR_OK);
    CHECK(infr_analyze(s, &m) == INFR_ERR_SAMPLES);
    for (i = 0; i < 200; i++)
        add_two_reports(s, i);
    CHECK(infr_add(s, INFR_FREE, bad_len, 2) == INFR_ERR_LEN);
    CHECK(infr_add(s, INFR_FREE, zero, 2) == INFR_ERR_ARG);
    CHECK(infr_add(s, INFR_FREE, buf, 4) == INFR_ERR_LEN);
    m = analyze(s);
    CHECK(infr_field(m, 0)->report_id == 5 && infr_field(m, 0)->kind == INFR_CONSTANT);
    CHECK(infr_field(m, infr_field_count(m) - 1)->report_id == 7);
    CHECK(infr_descriptor(m, buf, sizeof(buf), &len) == INFR_OK);
    CHECK(buf[6] == 0x85 && buf[7] == 0x05);
    CHECK(memchr(buf + 8, 0x85, len - 8) != NULL && ((unsigned char *)memchr(buf + 8, 0x85, len - 8))[1] == 0x07);
    CHECK(infr_normalize(m, other, 2, out, sizeof(out)) == INFR_ERR_LEN);
    CHECK(infr_normalize(m, bad_len, 2, out, sizeof(out)) == INFR_ERR_LEN);
    infr_model_destroy(m);
    CHECK(infr_report_count(s) == 2);
    {
        unsigned id = 0;
        size_t rlen = 0;
        size_t n = 0;

        CHECK(infr_report_info(s, 1, &id, &rlen, &n) == INFR_OK && id == 7 && rlen == 2 && n == 200);
        CHECK(infr_report_info(s, 2, &id, &rlen, &n) == INFR_ERR_ARG);
        CHECK(infr_report_info(s, 0, NULL, NULL, NULL) == INFR_OK);
    }
    CHECK(infr_add(s, INFR_FREE, other, 2) == INFR_OK);
    m = analyze(s);
    CHECK(infr_field(m, infr_field_count(m) - 1)->report_id == 7);
    CHECK(infr_normalize(m, other, 2, out, sizeof(out)) == INFR_ERR_LEN);
    CHECK(infr_descriptor(m, buf, sizeof(buf), &len) == INFR_OK);
    for (i = 0; i + 1 < len; i++)
        CHECK(!(buf[i] == 0x85 && buf[i + 1] == 0x09));
    infr_model_destroy(m);
    for (i = 10; i < 10 + INFR_MAX_REPORTS - 3; i++) {
        buf[0] = (unsigned char)i;
        CHECK(infr_add(s, INFR_FREE, buf, 1) == INFR_OK);
    }
    buf[0] = 0xff;
    CHECK(infr_add(s, INFR_FREE, buf, 1) == INFR_ERR_FULL);
    infr_destroy(s);
}

static unsigned ref_crc8(const unsigned char *r, size_t len)
{
    unsigned crc = 0;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= r[i];
        for (b = 0; b < 8; b++)
            crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
    return crc;
}

static uint32_t ref_crc32(const unsigned char *r, size_t len)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= r[i];
        for (b = 0; b < 8; b++)
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return ~crc;
}

static void fill_checksum(unsigned algorithm, unsigned char *r)
{
    uint32_t crc;

    if (algorithm == INFR_XOR8)
        r[4] = (unsigned char)(r[0] ^ r[1] ^ r[2] ^ r[3]);
    else if (algorithm == INFR_CRC8)
        r[4] = (unsigned char)ref_crc8(r, 4);
    crc = ref_crc32(r, 5);
    if (algorithm == INFR_CRC32) {
        r[5] = (unsigned char)crc;
        r[6] = (unsigned char)(crc >> 8);
        r[7] = (unsigned char)(crc >> 16);
        r[8] = (unsigned char)(crc >> 24);
    }
}

static void check_algorithm(unsigned algorithm, size_t offset)
{
    unsigned char r[9];
    infr_session_t *s = NULL;
    synth_t st = {7, 0, 0, 0};
    const infr_field_t *f;
    infr_model_t *m;
    size_t i;
    size_t j;

    CHECK(infr_create(sizeof(r), 300, NULL, &s) == INFR_OK);
    for (i = 0; i < 300; i++) {
        memset(r, 0, sizeof(r));
        for (j = 0; j < 4; j++)
            r[j] = (unsigned char)synth_rand(&st, 256);
        fill_checksum(algorithm, r);
        CHECK(infr_add(s, INFR_FREE, r, sizeof(r)) == INFR_OK);
    }
    m = analyze(s);
    f = find_field(m, offset);
    CHECK(f != NULL && f->kind == INFR_CHECKSUM && f->algorithm == algorithm);
    infr_model_destroy(m);
    infr_destroy(s);
}

static void test_checksums(void)
{
    check_algorithm(INFR_XOR8, 32);
    check_algorithm(INFR_CRC8, 32);
    check_algorithm(INFR_CRC32, 40);
}

static infr_model_t *walk_model(size_t len, void (*fill)(synth_t *, size_t, unsigned char *))
{
    unsigned char r[16];
    infr_session_t *s = NULL;
    infr_model_t *m;
    synth_t st = {3, 0, 0, 0};
    size_t i;

    CHECK(infr_create(len, 600, NULL, &s) == INFR_OK);
    for (i = 0; i < 600; i++) {
        memset(r, 0, sizeof(r));
        fill(&st, i, r);
        CHECK(infr_add(s, i < 100 ? INFR_REST : INFR_FREE, r, len) == INFR_OK);
    }
    m = analyze(s);
    infr_destroy(s);
    return m;
}

static void fill_xor_tail(synth_t *st, size_t i, unsigned char *r)
{
    r[0] = 0x01;
    r[1] = (unsigned char)(i >= 100 && i % 20 < 10);
    st->angle = i < 100 ? 0 : (st->angle + 7) % 256;
    r[2] = (unsigned char)st->angle;
    r[3] = (unsigned char)(255 - st->angle / 2);
    r[4] = (unsigned char)(r[0] ^ r[1] ^ r[2] ^ r[3]);
}

static void fill_twin_bytes(synth_t *st, size_t i, unsigned char *r)
{
    st->angle = i < 100 ? 128 : (st->angle + 5) % 256;
    r[0] = (unsigned char)st->angle;
    r[1] = r[0];
}

static void fill_wide(synth_t *st, size_t i, unsigned char *r)
{
    uint32_t v;

    st->tilt += i < 100 ? 0 : (int)synth_rand(st, 2001) - 1000;
    v = (uint32_t)st->tilt;
    r[0] = (unsigned char)v;
    r[1] = (unsigned char)(v >> 8);
    r[2] = (unsigned char)(v >> 16);
    r[3] = (unsigned char)(v >> 24);
}

static void fill_step3(synth_t *st, size_t i, unsigned char *r)
{
    (void)st;
    r[0] = (unsigned char)(i * 3);
    r[1] = 0x40;
}

static void test_checksum_false_positives(void)
{
    infr_model_t *s = walk_model(5, fill_xor_tail);
    const infr_field_t *f = find_field(s, 32);

    CHECK(f != NULL && f->kind == INFR_CHECKSUM && f->algorithm == INFR_XOR8);
    CHECK(find_field(s, 8) != NULL && find_field(s, 8)->kind == INFR_BUTTON);
    CHECK(find_field(s, 16) != NULL && find_field(s, 16)->kind == INFR_AXIS);
    infr_model_destroy(s);
    s = walk_model(2, fill_twin_bytes);
    CHECK(find_field(s, 0)->kind == INFR_AXIS && find_field(s, 8)->kind == INFR_AXIS);
    infr_model_destroy(s);
}

static void test_wide_axis(void)
{
    static const unsigned char range[] = {0x17, 0x00, 0x00, 0x00, 0x80, 0x27, 0xff, 0xff, 0xff, 0x7f};
    unsigned char buf[128];
    size_t len = 0;
    infr_model_t *s = walk_model(4, fill_wide);
    const infr_field_t *f = find_field(s, 0);

    CHECK(f != NULL && f->kind == INFR_AXIS && f->bit_size == 32 && (f->flags & INFR_SIGNED));
    CHECK(f->logical_min < 0 && f->logical_max > 0);
    CHECK(infr_descriptor(s, buf, sizeof(buf), &len) == INFR_OK);
    CHECK(len > sizeof(range));
    CHECK(memcmp(buf + 8, range, sizeof(range)) == 0);
    infr_model_destroy(s);
}

static void test_counter_step(void)
{
    infr_model_t *s = walk_model(2, fill_step3);
    const infr_field_t *f = find_field(s, 0);

    CHECK(f != NULL && f->kind == INFR_COUNTER && f->bit_size == 8 && f->step == 3);
    infr_model_destroy(s);
}

static void test_label_limit(void)
{
    static const unsigned char r[1] = {0};
    infr_session_t *s = NULL;
    infr_model_t *m;
    int label;

    CHECK(infr_create(1, 200, NULL, &s) == INFR_OK);
    for (label = 1; label <= INFR_MAX_LABELS; label++)
        CHECK(infr_add(s, label, r, 1) == INFR_OK);
    CHECK(infr_add(s, INFR_MAX_LABELS + 1, r, 1) == INFR_ERR_FULL);
    CHECK(infr_add(s, 1, r, 1) == INFR_OK);
    CHECK(infr_add(s, INFR_REST, r, 1) == INFR_OK);
    m = analyze(s);
    CHECK(infr_set_usage(m, 1, 0, 1) == INFR_ERR_ARG);
    for (label = 1; label <= INFR_MAX_HINTS; label++)
        CHECK(infr_set_usage(m, label, 0x09, (uint32_t)label) == INFR_OK);
    CHECK(infr_set_usage(m, INFR_MAX_HINTS + 1, 0x09, 1) == INFR_ERR_FULL);
    CHECK(infr_set_usage(m, 1, 0x09, 7) == INFR_OK);
    infr_model_destroy(m);
    infr_destroy(s);
}

static void test_label_limit_new_report(void)
{
    static const infr_options_t ids = {INFR_REPORT_IDS, NULL, NULL, NULL};
    unsigned char a[2] = {1, 0};
    unsigned char b[2] = {2, 0};
    infr_session_t *s = NULL;
    int label;

    CHECK(infr_create(2, 100, &ids, &s) == INFR_OK);
    for (label = 1; label <= INFR_MAX_LABELS; label++) {
        a[1] = (unsigned char)label;
        CHECK(infr_add(s, label, a, 2) == INFR_OK);
    }
    CHECK(infr_add(s, INFR_MAX_LABELS + 1, b, 2) == INFR_ERR_FULL);
    CHECK(infr_report_count(s) == 1);
    infr_model_destroy(analyze(s));
    infr_destroy(s);
}

static void test_hat_rest_15(void)
{
    unsigned char r[1];
    infr_session_t *s = NULL;
    infr_model_t *m;
    size_t i;
    int with_eight;

    for (with_eight = 0; with_eight < 2; with_eight++) {
        CHECK(infr_create(1, 300, NULL, &s) == INFR_OK);
        for (i = 0; i < 300; i++) {
            r[0] = (unsigned char)(i < 100 || i % 30 >= 20 ? 0x0f : (i / 30) % (8u + (unsigned)with_eight));
            CHECK(infr_add(s, i < 100 ? INFR_REST : 1, r, 1) == INFR_OK);
        }
        m = analyze(s);
        CHECK((find_field(m, 0)->kind == INFR_HAT) == !with_eight);
        infr_model_destroy(m);
        infr_destroy(s);
    }
}

static void test_strerror(void)
{
    int codes[] = {INFR_OK, INFR_ERR_ARG, INFR_ERR_NOMEM, INFR_ERR_LEN, INFR_ERR_FULL,
                   INFR_ERR_SAMPLES, INFR_ERR_STATE, INFR_ERR_SPACE, -1, 999};
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        CHECK(infr_strerror(codes[i]) != NULL && infr_strerror(codes[i])[0] != '\0');
    CHECK(strcmp(infr_strerror(INFR_ERR_ARG), infr_strerror(INFR_OK)) != 0);
}

static void test_alloc_failure(void)
{
    static const unsigned char r[4] = {1, 2, 3, 4};
    int n;

    for (n = 0;; n++) {
        failing_alloc_t fa = {n, 0};
        infr_options_t options = {0, failing_alloc, failing_free, NULL};
        infr_session_t *s = NULL;
        infr_model_t *m = NULL;
        int status;

        options.alloc_ctx = &fa;
        status = infr_create(4, 8, &options, &s);

        if (status == INFR_OK) {
            CHECK(infr_add(s, 0, r, 4) == INFR_OK);
            CHECK(infr_add(s, 0, r, 4) == INFR_OK);
            status = infr_analyze(s, &m);
            CHECK(status == INFR_OK || (status == INFR_ERR_NOMEM && m == NULL));
        }
        infr_model_destroy(m);
        infr_destroy(s);
        CHECK(fa.live == 0);
        if (status == INFR_OK)
            break;
        CHECK(status == INFR_ERR_NOMEM);
    }
    CHECK(n > 3);
}

int main(void)
{
    test_create();
    test_add();
    test_analyze();
    test_field();
    test_set_usage();
    test_normalize();
    test_read();
    test_descriptor();
    test_report_id();
    test_checksums();
    test_checksum_false_positives();
    test_wide_axis();
    test_counter_step();
    test_label_limit();
    test_hat_rest_15();
    test_label_limit_new_report();
    test_strerror();
    test_alloc_failure();
    return 0;
}
