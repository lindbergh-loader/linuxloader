#ifndef NAMCO_ES1_WMMT4_PB_H
#define NAMCO_ES1_WMMT4_PB_H

#include <stddef.h>
#include <stdint.h>

// Protobuf messages for WMMT4's server, held as their fields (number, wire
// type, value or bytes), without a schema: a request is read field by
// field, an answer or a stored record built or changed field by field. The
// field numbers are those of docs/wmmt4-proto/.

#define PB_VARINT 0
#define PB_FIXED64 1
#define PB_BYTES 2
#define PB_FIXED32 5

typedef struct
{
    uint32_t field;
    uint8_t wire;
    uint64_t value; // varint, fixed32 and fixed64
    uint8_t *data;  // bytes (strings, messages, packed), owned
    size_t length;
} PbField;

typedef struct
{
    PbField *fields;
    size_t count, capacity;
} PbMsg;

// Parse (appending to msg): 0 if the data is not a message.
int pbParse(PbMsg *msg, const uint8_t *data, size_t length);
void pbFree(PbMsg *msg);
// The serialised message (malloc'd), its fields in their order.
uint8_t *pbSerialize(const PbMsg *msg, size_t *length);

// The last occurrence of a field (proto2: the last one wins), or NULL.
const PbField *pbGet(const PbMsg *msg, uint32_t field);
int pbHas(const PbMsg *msg, uint32_t field);
uint64_t pbUint(const PbMsg *msg, uint32_t field, uint64_t fallback);
int32_t pbSint(const PbMsg *msg, uint32_t field, int32_t fallback); // sint32 (zigzag)
// A string field into buffer ("" when absent).
const char *pbString(const PbMsg *msg, uint32_t field, char *buffer, size_t size);
// A message field, parsed into sub (empty when absent): 0 when absent.
int pbSub(const PbMsg *msg, uint32_t field, PbMsg *sub);

// Set: the field's occurrences replaced by the one value. Add: one more
// occurrence (repeated fields).
void pbRemove(PbMsg *msg, uint32_t field);
void pbSetUint(PbMsg *msg, uint32_t field, uint64_t value);
void pbSetSint(PbMsg *msg, uint32_t field, int32_t value);
void pbSetBytes(PbMsg *msg, uint32_t field, const void *data, size_t length);
void pbSetString(PbMsg *msg, uint32_t field, const char *text);
void pbSetMsg(PbMsg *msg, uint32_t field, const PbMsg *sub);
void pbAddUint(PbMsg *msg, uint32_t field, uint64_t value);
void pbAddBytes(PbMsg *msg, uint32_t field, const void *data, size_t length);
void pbAddMsg(PbMsg *msg, uint32_t field, const PbMsg *sub);
// A field set as it is in another message (when there).
void pbCopy(PbMsg *to, const PbMsg *from, uint32_t field);
// A field set to a value unless it is there already.
void pbDefaultUint(PbMsg *msg, uint32_t field, uint64_t value);

#endif // NAMCO_ES1_WMMT4_PB_H
