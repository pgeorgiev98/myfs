#ifndef HELPERS_H_INCLUDED
#define HELPERS_H_INCLUDED

#include <stdint.h>
#include "myfs.h"

struct indirect_block_count_t
{
	uint32_t singly_indirect;
	uint32_t doubly_indirect;
	uint32_t triply_indirect;
	uint32_t total_indirect;
};

struct indirect_block_count_t calc_indirect_block_count(uint16_t block_size, uint32_t block_count);

/* Read the pos-th u32 from the block_id-th block in fd */
void read_u32_from_block(int fd, struct fsinfo_t *fs, uint32_t block_id, uint16_t pos, uint32_t *value);

/* Write the pos-th u32 to the block_id-th block in fd */
void write_u32_to_block(int fd, struct fsinfo_t *fs, uint32_t block_id, uint16_t pos, uint32_t value);

#endif
