// Protobuf messages held as their fields (see namcoEs1Wmmt4Pb.h).

#include <stdlib.h>
#include <string.h>

#include "namcoEs1Wmmt4Pb.h"

static int readVarint(const uint8_t **p, const uint8_t *end, uint64_t *value)
{
    uint64_t v = 0;
    for (int shift = 0; *p < end && shift < 64; shift += 7)
    {
        uint8_t c = *(*p)++;
        v |= (uint64_t)(c & 0x7f) << shift;
        if (!(c & 0x80))
        {
            *value = v;
            return 1;
        }
    }
    return 0;
}

static PbField *append(PbMsg *msg)
{
    if (msg->count == msg->capacity)
    {
        size_t capacity = msg->capacity ? msg->capacity * 2 : 16;
        PbField *fields = realloc(msg->fields, capacity * sizeof(*fields));
        if (!fields)
            return NULL;
        msg->fields = fields;
        msg->capacity = capacity;
    }
    PbField *f = &msg->fields[msg->count++];
    memset(f, 0, sizeof(*f));
    return f;
}

int pbParse(PbMsg *msg, const uint8_t *data, size_t length)
{
    const uint8_t *p = data, *end = data + length;

    if (!data && length)
        return 0;
    while (p < end)
    {
        uint64_t key, value = 0;
        if (!readVarint(&p, end, &key) || (key >> 3) == 0)
            return 0;
        PbField f = {(uint32_t)(key >> 3), (uint8_t)(key & 7), 0, NULL, 0};
        switch (f.wire)
        {
        case PB_VARINT:
            if (!readVarint(&p, end, &value))
                return 0;
            f.value = value;
            break;
        case PB_FIXED64:
            if (end - p < 8)
                return 0;
            memcpy(&f.value, p, 8);
            p += 8;
            break;
        case PB_FIXED32:
        {
            uint32_t v;
            if (end - p < 4)
                return 0;
            memcpy(&v, p, 4);
            f.value = v;
            p += 4;
            break;
        }
        case PB_BYTES:
            if (!readVarint(&p, end, &value) || value > (uint64_t)(end - p))
                return 0;
            f.length = (size_t)value;
            f.data = malloc(f.length + 1);
            if (!f.data)
                return 0;
            memcpy(f.data, p, f.length);
            f.data[f.length] = 0;
            p += f.length;
            break;
        default:
            return 0;
        }
        PbField *slot = append(msg);
        if (!slot)
        {
            free(f.data);
            return 0;
        }
        *slot = f;
    }
    return 1;
}

void pbFree(PbMsg *msg)
{
    for (size_t i = 0; i < msg->count; i++)
        free(msg->fields[i].data);
    free(msg->fields);
    memset(msg, 0, sizeof(*msg));
}

typedef struct
{
    uint8_t *data;
    size_t length, capacity;
} Out;

static void put(Out *o, const void *data, size_t length)
{
    if (o->length + length > o->capacity)
    {
        size_t capacity = (o->length + length) * 2 + 64;
        uint8_t *d = realloc(o->data, capacity);
        if (!d)
            return;
        o->data = d;
        o->capacity = capacity;
    }
    memcpy(o->data + o->length, data, length);
    o->length += length;
}

static void putVarint(Out *o, uint64_t v)
{
    uint8_t b[10];
    size_t n = 0;
    do
    {
        b[n] = v & 0x7f;
        v >>= 7;
        if (v)
            b[n] |= 0x80;
        n++;
    } while (v);
    put(o, b, n);
}

uint8_t *pbSerialize(const PbMsg *msg, size_t *length)
{
    Out o = {0};
    for (size_t i = 0; i < msg->count; i++)
    {
        const PbField *f = &msg->fields[i];
        putVarint(&o, (uint64_t)f->field << 3 | f->wire);
        switch (f->wire)
        {
        case PB_VARINT:
            putVarint(&o, f->value);
            break;
        case PB_FIXED64:
            put(&o, &f->value, 8);
            break;
        case PB_FIXED32:
        {
            uint32_t v = (uint32_t)f->value;
            put(&o, &v, 4);
            break;
        }
        case PB_BYTES:
            putVarint(&o, f->length);
            if (f->length)
                put(&o, f->data, f->length);
            break;
        }
    }
    if (!o.data)
        o.data = malloc(1);
    *length = o.length;
    return o.data;
}

const PbField *pbGet(const PbMsg *msg, uint32_t field)
{
    for (size_t i = msg->count; i-- > 0;)
        if (msg->fields[i].field == field)
            return &msg->fields[i];
    return NULL;
}

int pbHas(const PbMsg *msg, uint32_t field)
{
    return pbGet(msg, field) != NULL;
}

uint64_t pbUint(const PbMsg *msg, uint32_t field, uint64_t fallback)
{
    const PbField *f = pbGet(msg, field);
    return f && f->wire != PB_BYTES ? f->value : fallback;
}

int32_t pbSint(const PbMsg *msg, uint32_t field, int32_t fallback)
{
    const PbField *f = pbGet(msg, field);
    if (!f || f->wire != PB_VARINT)
        return fallback;
    uint32_t v = (uint32_t)f->value;
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
}

const char *pbString(const PbMsg *msg, uint32_t field, char *buffer, size_t size)
{
    const PbField *f = pbGet(msg, field);
    size_t n = f && f->wire == PB_BYTES ? f->length : 0;
    if (n >= size)
        n = size - 1;
    if (n)
        memcpy(buffer, f->data, n);
    buffer[n] = 0;
    return buffer;
}

int pbSub(const PbMsg *msg, uint32_t field, PbMsg *sub)
{
    const PbField *f = pbGet(msg, field);
    memset(sub, 0, sizeof(*sub));
    if (!f || f->wire != PB_BYTES)
        return 0;
    if (!pbParse(sub, f->data, f->length))
    {
        pbFree(sub);
        return 0;
    }
    return 1;
}

void pbRemove(PbMsg *msg, uint32_t field)
{
    size_t n = 0;
    for (size_t i = 0; i < msg->count; i++)
    {
        if (msg->fields[i].field == field)
            free(msg->fields[i].data);
        else
            msg->fields[n++] = msg->fields[i];
    }
    msg->count = n;
}

void pbAddUint(PbMsg *msg, uint32_t field, uint64_t value)
{
    PbField *f = append(msg);
    if (f)
    {
        f->field = field;
        f->wire = PB_VARINT;
        f->value = value;
    }
}

void pbAddBytes(PbMsg *msg, uint32_t field, const void *data, size_t length)
{
    PbField *f = append(msg);
    if (!f)
        return;
    f->field = field;
    f->wire = PB_BYTES;
    f->data = malloc(length + 1);
    if (!f->data)
    {
        msg->count--;
        return;
    }
    if (length)
        memcpy(f->data, data, length);
    f->data[length] = 0;
    f->length = length;
}

void pbAddMsg(PbMsg *msg, uint32_t field, const PbMsg *sub)
{
    size_t length;
    uint8_t *data = pbSerialize(sub, &length);
    pbAddBytes(msg, field, data, length);
    free(data);
}

void pbSetUint(PbMsg *msg, uint32_t field, uint64_t value)
{
    pbRemove(msg, field);
    pbAddUint(msg, field, value);
}

void pbSetSint(PbMsg *msg, uint32_t field, int32_t value)
{
    pbSetUint(msg, field, (uint32_t)((value << 1) ^ (value >> 31)));
}

void pbSetBytes(PbMsg *msg, uint32_t field, const void *data, size_t length)
{
    // data may be in the field replaced
    uint8_t *copy = malloc(length + 1);
    if (!copy)
        return;
    if (length)
        memcpy(copy, data, length);
    pbRemove(msg, field);
    pbAddBytes(msg, field, copy, length);
    free(copy);
}

void pbSetString(PbMsg *msg, uint32_t field, const char *text)
{
    pbSetBytes(msg, field, text, strlen(text));
}

void pbSetMsg(PbMsg *msg, uint32_t field, const PbMsg *sub)
{
    size_t length;
    uint8_t *data = pbSerialize(sub, &length);
    pbSetBytes(msg, field, data, length);
    free(data);
}

void pbCopy(PbMsg *to, const PbMsg *from, uint32_t field)
{
    const PbField *f = pbGet(from, field);
    if (!f)
        return;
    if (f->wire == PB_BYTES)
        pbSetBytes(to, field, f->data, f->length);
    else
    {
        pbRemove(to, field);
        PbField *g = append(to);
        if (g)
        {
            g->field = field;
            g->wire = f->wire;
            g->value = f->value;
        }
    }
}

void pbDefaultUint(PbMsg *msg, uint32_t field, uint64_t value)
{
    if (!pbHas(msg, field))
        pbAddUint(msg, field, value);
}
