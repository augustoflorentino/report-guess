#include "infer_int.h"

#define INFR_SEED_NONE (-1)
#define INFR_SEEDS 256

static uint8_t infr_crc8(const unsigned char *p, size_t len, int maxim)
{
    uint8_t crc = 0;
    size_t i;
    int k;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            if (maxim)
                crc = (uint8_t)(crc & 1 ? (crc >> 1) ^ 0x8c : crc >> 1);
            else
                crc = (uint8_t)(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
        }
    }
    return crc;
}

static uint32_t infr_crc32_update(uint32_t crc, const unsigned char *p, size_t len)
{
    size_t i;
    int k;

    for (i = 0; i < len; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++)
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return crc;
}

static uint32_t infr_crc32(const unsigned char *p, size_t len, int seed)
{
    uint32_t crc = 0xffffffffu;
    unsigned char first = (unsigned char)seed;

    if (seed != INFR_SEED_NONE)
        crc = infr_crc32_update(crc, &first, 1);
    return ~infr_crc32_update(crc, p, len);
}

static uint8_t infr_sum8(const unsigned char *p, size_t len, size_t skip, int use_xor)
{
    uint8_t acc = 0;
    size_t i;

    for (i = 0; i < len; i++) {
        if (i == skip)
            continue;
        acc = (uint8_t)(use_xor ? acc ^ p[i] : acc + p[i]);
    }
    return acc;
}

static uint8_t infr_check8(const unsigned char *r, size_t len, size_t pos, unsigned algorithm,
                           size_t covered)
{
    size_t skip = covered == len ? pos : len;

    switch (algorithm) {
    case INFR_SUM8:
        return infr_sum8(r, covered, skip, 0);
    case INFR_SUM8_NEG:
        return (uint8_t)(0u - infr_sum8(r, covered, skip, 0));
    case INFR_XOR8:
        return infr_sum8(r, covered, skip, 1);
    case INFR_CRC8:
        return infr_crc8(r, covered, 0);
    default:
        return infr_crc8(r, covered, 1);
    }
}

static int infr_byte_varies(const infr_session_t *s, size_t pos, size_t bytes)
{
    size_t b;

    if (!infr_window_free(s, pos * 8, (unsigned)(bytes * 8)))
        return 0;
    for (b = pos * 8; b < (pos + bytes) * 8; b++) {
        if (s->bit_changes[b] > 0)
            return 1;
    }
    return 0;
}

static int infr_matches8(const infr_session_t *s, size_t pos, unsigned algorithm, size_t covered)
{
    size_t i;
    size_t misses = 0;

    for (i = 0; i < s->count; i++) {
        const unsigned char *r = infr_sample(s, i);

        misses += r[pos] != infr_check8(r, s->report_len, pos, algorithm, covered);
        if (misses * 100 > s->count)
            return 0;
    }
    return 1;
}

static int infr_matches32(const infr_session_t *s, size_t pos, int seed)
{
    size_t i;
    size_t misses = 0;

    for (i = 0; i < s->count; i++) {
        const unsigned char *r = infr_sample(s, i);

        misses += infr_raw(r, pos * 8, 32, 0) != infr_crc32(r, pos, seed);
        if (misses * 100 > s->count)
            return 0;
    }
    return 1;
}

static infr_field_t *infr_add_checksum(infr_session_t *s, size_t pos, unsigned bytes, unsigned algorithm,
                                       size_t covered)
{
    infr_field_t *f = infr_new_field(s, pos * 8, bytes * 8, INFR_CHECKSUM);

    f->algorithm = algorithm;
    f->covered_len = covered;
    return f;
}

static uint32_t infr_seed_state(int seed)
{
    unsigned char first = (unsigned char)seed;

    if (seed == INFR_SEED_NONE)
        return 0xffffffffu;
    return infr_crc32_update(0xffffffffu, &first, 1);
}

static size_t infr_find_crc32(const infr_session_t *s, int *out_seed)
{
    const unsigned char *r = infr_sample(s, 0);
    size_t best = 0;
    int seed;

    for (seed = INFR_SEED_NONE; seed < INFR_SEEDS; seed++) {
        uint32_t crc = infr_seed_state(seed);
        size_t pos;

        for (pos = 1; pos + 4 <= s->report_len; pos++) {
            crc = infr_crc32_update(crc, r + pos - 1, 1);
            if (pos > best && ~crc == infr_raw(r, pos * 8, 32, 0) && infr_byte_varies(s, pos, 4) &&
                infr_matches32(s, pos, seed)) {
                best = pos;
                *out_seed = seed;
            }
        }
    }
    return best;
}

static int infr_mixes_bytes(const infr_session_t *s, size_t pos, size_t covered)
{
    size_t varying = 0;
    size_t b;

    for (b = 0; b < covered && varying < 2; b++)
        varying += b != pos && infr_byte_varies(s, b, 1);
    return varying >= 2;
}

static int infr_try_check8(infr_session_t *s, size_t pos)
{
    unsigned algorithm;
    size_t covers[2];
    size_t c;

    covers[0] = pos;
    covers[1] = s->report_len;
    for (algorithm = INFR_SUM8; algorithm <= INFR_CRC8_MAXIM; algorithm++) {
        for (c = 0; c < 2; c++) {
            if (covers[c] == 0 || (c == 1 && algorithm >= INFR_CRC8) || !infr_mixes_bytes(s, pos, covers[c]))
                continue;
            if (infr_matches8(s, pos, algorithm, covers[c])) {
                infr_add_checksum(s, pos, 1, algorithm, covers[c]);
                return 1;
            }
        }
    }
    return 0;
}

void infr_find_checksums(infr_session_t *s)
{
    int seed = INFR_SEED_NONE;
    size_t crc_pos = infr_find_crc32(s, &seed);
    size_t pos;

    for (pos = s->report_len; pos-- > 0;) {
        if (crc_pos > 0 && pos > crc_pos && pos < crc_pos + 4)
            continue;
        if (crc_pos > 0 && pos == crc_pos) {
            infr_add_checksum(s, pos, 4, INFR_CRC32, pos)->seed = seed;
            return;
        }
        if (infr_byte_varies(s, pos, 1) && infr_try_check8(s, pos))
            return;
    }
}
