#include "infer_int.h"

#define INFR_TYPE_MAIN 0u
#define INFR_TYPE_GLOBAL 1u
#define INFR_TYPE_LOCAL 2u
#define INFR_PADDING_CHUNK 4096u

typedef struct {
    unsigned char *buf;
    size_t cap;
    size_t len;
    uint32_t page;
    size_t padding;
    int globals_set;
    int32_t min;
    int32_t max;
    unsigned size;
    uint32_t count;
} infr_writer_t;

static void infr_put(infr_writer_t *w, unsigned char byte)
{
    if (w->len < w->cap)
        w->buf[w->len] = byte;
    w->len++;
}

static void infr_item(infr_writer_t *w, unsigned type, unsigned tag, uint32_t value, unsigned size)
{
    unsigned code = size == 4 ? 3 : size;
    unsigned i;

    infr_put(w, (unsigned char)(tag << 4 | type << 2 | code));
    for (i = 0; i < size; i++)
        infr_put(w, (unsigned char)(value >> (8 * i)));
}

static void infr_unsigned_item(infr_writer_t *w, unsigned type, unsigned tag, uint32_t value)
{
    infr_item(w, type, tag, value, value <= 0xff ? 1 : value <= 0xffff ? 2 : 4);
}

static void infr_signed_item(infr_writer_t *w, unsigned tag, int32_t value)
{
    unsigned size = value >= -128 && value <= 127 ? 1 : value >= -32768 && value <= 32767 ? 2 : 4;

    infr_item(w, INFR_TYPE_GLOBAL, tag, (uint32_t)value, size);
}

static void infr_usage(infr_writer_t *w, uint32_t page, uint32_t usage)
{
    if (page != w->page)
        infr_unsigned_item(w, INFR_TYPE_GLOBAL, 0x0, page);
    w->page = page;
    infr_unsigned_item(w, INFR_TYPE_LOCAL, 0x0, usage);
}

static void infr_size_count(infr_writer_t *w, unsigned size, uint32_t count)
{
    if (!w->globals_set || size != w->size)
        infr_unsigned_item(w, INFR_TYPE_GLOBAL, 0x7, size);
    if (!w->globals_set || count != w->count)
        infr_unsigned_item(w, INFR_TYPE_GLOBAL, 0x9, count);
    w->size = size;
    w->count = count;
}

static void infr_main_input(infr_writer_t *w, int32_t min, int32_t max, unsigned size, unsigned flags)
{
    if (!w->globals_set || min != w->min)
        infr_signed_item(w, 0x1, min);
    if (!w->globals_set || max != w->max)
        infr_signed_item(w, 0x2, max);
    infr_size_count(w, size, 1);
    w->min = min;
    w->max = max;
    w->globals_set = 1;
    infr_unsigned_item(w, INFR_TYPE_MAIN, 0x8, flags);
}

static void infr_flush_padding(infr_writer_t *w)
{
    while (w->padding > 0) {
        uint32_t chunk = w->padding > INFR_PADDING_CHUNK ? INFR_PADDING_CHUNK : (uint32_t)w->padding;
        int was_set = w->globals_set;

        infr_size_count(w, 1, chunk);
        w->globals_set = was_set;
        infr_unsigned_item(w, INFR_TYPE_MAIN, 0x8, 0x03);
        w->padding -= chunk;
    }
}

static void infr_axis_range(const infr_field_t *f, int32_t *min, int32_t *max)
{
    unsigned size = f->bit_size;

    if (f->flags & INFR_SIGNED) {
        *max = (int32_t)((1u << (size - 1)) - 1);
        *min = -*max - 1;
    } else {
        *min = 0;
        *max = (int32_t)((1u << size) - 1);
    }
}

static void infr_write_field(infr_writer_t *w, const infr_field_t *f)
{
    int32_t min;
    int32_t max;

    if (f->kind != INFR_BUTTON && f->kind != INFR_AXIS && f->kind != INFR_HAT) {
        w->padding += f->bit_size;
        return;
    }
    infr_flush_padding(w);
    infr_usage(w, f->usage_page, f->usage);
    if (f->kind == INFR_BUTTON) {
        infr_main_input(w, 0, 1, 1, 0x02);
    } else if (f->kind == INFR_HAT) {
        infr_main_input(w, f->logical_min, f->logical_max, f->bit_size, 0x42);
    } else {
        infr_axis_range(f, &min, &max);
        infr_main_input(w, min, max, f->bit_size, 0x02);
    }
}

static void infr_write_body(infr_writer_t *w, const infr_session_t *s, size_t start_bit)
{
    size_t i;

    for (i = 0; i < s->field_count; i++) {
        const infr_field_t *f = &s->fields[i];

        if (f->bit_offset < start_bit)
            continue;
        infr_write_field(w, f);
    }
    infr_flush_padding(w);
}

static int infr_starts_with_id(const infr_session_t *s, unsigned report_id)
{
    const infr_field_t *f = s->field_count > 0 ? &s->fields[0] : NULL;

    return f != NULL && f->kind == INFR_CONSTANT && f->bit_size == 8 && f->rest == (int32_t)report_id;
}

int infr_descriptor(const infr_session_t *session, unsigned report_id, unsigned char *out,
                    size_t out_len, size_t *out_written)
{
    infr_writer_t w;

    if (session == NULL || out_written == NULL || (out == NULL && out_len > 0) || report_id > 255)
        return INFR_ERR_ARG;
    *out_written = 0;
    if (!session->analyzed)
        return INFR_ERR_STATE;
    if (report_id != 0 && !infr_starts_with_id(session, report_id))
        return INFR_ERR_ARG;
    w.buf = out;
    w.cap = out_len;
    w.len = 0;
    w.page = 0x01;
    w.padding = 0;
    w.globals_set = 0;
    infr_item(&w, INFR_TYPE_GLOBAL, 0x0, 0x01, 1);
    infr_item(&w, INFR_TYPE_LOCAL, 0x0, 0x05, 1);
    infr_item(&w, INFR_TYPE_MAIN, 0xa, 0x01, 1);
    if (report_id != 0)
        infr_item(&w, INFR_TYPE_GLOBAL, 0x8, report_id, 1);
    infr_write_body(&w, session, report_id != 0 ? 8 : 0);
    infr_item(&w, INFR_TYPE_MAIN, 0xc, 0, 0);
    *out_written = w.len;
    return w.len <= out_len ? INFR_OK : INFR_ERR_SPACE;
}
