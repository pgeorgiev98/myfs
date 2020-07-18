#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

// ceil(A/B)
#define CEIL_DIV(A, B) ((A)/(B) + ((A)%(B) != 0))

static inline void util_write_u16(uint8_t *out, uint16_t v)
{
	*(uint16_t *)out = v;
}

static inline void util_write_u32(uint8_t *out, uint32_t v)
{
	*(uint32_t *)out = v;
}

static inline void util_write_u64(uint8_t *out, uint64_t v)
{
	*(uint64_t *)out = v;
}


static inline void util_read_u16(const uint8_t *in, uint16_t *v)
{
	*v = *(const uint16_t *)in;
}

static inline void util_read_u32(const uint8_t *in, uint32_t *v)
{
	*v = *(const uint32_t *)in;
}

static inline void util_read_u64(const uint8_t *in, uint64_t *v)
{
	*v = *(const uint64_t *)in;
}


static inline void util_writeseq_u16(uint8_t **out, uint16_t v)
{
	util_write_u16(*out, v);
	*out += sizeof(v);
}

static inline void util_writeseq_u32(uint8_t **out, uint32_t v)
{
	util_write_u32(*out, v);
	*out += sizeof(v);
}

static inline void util_writeseq_u64(uint8_t **out, uint64_t v)
{
	util_write_u64(*out, v);
	*out += sizeof(v);
}


static inline void util_readseq_u16(uint8_t **in, uint16_t *v)
{
	util_read_u16(*in, v);
	*in += sizeof(*v);
}

static inline void util_readseq_u32(uint8_t **in, uint32_t *v)
{
	util_read_u32(*in, v);
	*in += sizeof(*v);
}

static inline void util_readseq_u64(uint8_t **in, uint64_t *v)
{
	util_read_u64(*in, v);
	*in += sizeof(*v);
}

#endif
