#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <string.h>

// ceil(A/B)
#define CEIL_DIV(A, B) ((A)/(B) + ((A)%(B) != 0))

#if LITTLE_ENDIAN == 1

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

#else

#include <byteswap.h>

static inline void util_write_u16(uint8_t *out, uint16_t v)
{
	*(uint16_t *)out = bswap_16(v);
}

static inline void util_write_u32(uint8_t *out, uint32_t v)
{
	*(uint32_t *)out = bswap_32(v);
}

static inline void util_write_u64(uint8_t *out, uint64_t v)
{
	*(uint64_t *)out = bswap_64(v);
}


static inline void util_read_u16(const uint8_t *in, uint16_t *v)
{
	uint16_t t = *(const uint16_t *)in;
	*v = bswap_16(t);
}

static inline void util_read_u32(const uint8_t *in, uint32_t *v)
{
	uint32_t t = *(const uint32_t *)in;
	*v = bswap_32(t);
}

static inline void util_read_u64(const uint8_t *in, uint64_t *v)
{
	uint64_t t = *(const uint64_t *)in;
	*v = bswap_64(t);
}

#endif


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

static inline void util_split_path(const char *path, int len, char *parent_path, char *basename)
{
	int last_index;
	for (last_index = len - 1; last_index > 0; --last_index)
		if (path[last_index] == '/')
			break;
	memcpy(parent_path, path, last_index);
	parent_path[last_index] = '\0';

	int b_len = len - last_index - 1;
	memcpy(basename, path + last_index + 1, b_len);
	basename[b_len] = '\0';
}

#endif
