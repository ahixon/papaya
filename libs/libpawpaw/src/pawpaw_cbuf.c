#include <stdlib.h>
#include <string.h>
#include <pawpaw.h>
#include <assert.h>

struct pawpaw_cbuf {
    int count;
    int size;
    void* start;
    void* end;
    void* current;
};

struct pawpaw_cbuf* 
pawpaw_cbuf_create (int size, void* start) {
    struct pawpaw_cbuf* buf = malloc (sizeof (struct pawpaw_cbuf));
    if (!buf) {
        return NULL;
    }

    buf->count = 0;
    buf->size = size;
    buf->start = start;
    buf->end = start + size;
    buf->current = start;

    return buf;
}

void pawpaw_cbuf_destroy (struct pawpaw_cbuf* buf) {
    free (buf);
}

int
pawpaw_cbuf_full (struct pawpaw_cbuf* buf) {
    return buf->count == buf->size;
}

int
pawpaw_cbuf_empty (struct pawpaw_cbuf* buf) {
    return buf->count == 0;
}

int
pawpaw_cbuf_remaining (struct pawpaw_cbuf* buf) {
    return buf->size - buf->count;
}

int
pawpaw_cbuf_count (struct pawpaw_cbuf* buf) {
    return buf->count;
}

void
pawpaw_cbuf_write (struct pawpaw_cbuf* buf, void* data, int len) {
    /* can't go past end */
    if (buf->count + len > buf->size) {
        len = pawpaw_cbuf_remaining (buf);
    }

    void* after_write_end = buf->current + len;

    if (after_write_end > buf->end) {
        /* do to end and then rollover to start again */
        int until_end_amount = buf->end - buf->current;

        pawpaw_cbuf_write (buf, data, until_end_amount);
        pawpaw_cbuf_write (buf, data + until_end_amount, len - until_end_amount);
    } else {
        memcpy (buf->current, data, len);
        buf->current += len;
        buf->count += len;

        /* roll over */
        if (buf->current == buf->end) {
            buf->current = buf->start;
        }
    }
}

int
pawpaw_cbuf_read (struct pawpaw_cbuf* buf, void* dst_data, int len)  {
    /* don't allow underruns */
    if (len > buf->count) {
        len = buf->count;
    }

    if (len == 0) {
        return 0;
    }

    void* after_read_start = buf->current - len;
    if (after_read_start < buf->start) {
        /* read wraps around to end */
        int until_start_amount = buf->current - buf->start;

        pawpaw_cbuf_read (buf, dst_data, until_start_amount);
        assert (buf->current == buf->start);
        buf->current = buf->end;
        pawpaw_cbuf_read (buf, dst_data + until_start_amount, len - until_start_amount);
    } else {
        memcpy (dst_data, after_read_start, len);
        buf->current -= len;
        buf->count -= len;
    }

    return len;
}

/* FIXME: WRITE TESTS - specifically for wraparound cbuf_read */



