#include "helpers.h"
#include "util.h"

#include <unistd.h>

struct indirect_block_count_t calc_indirect_block_count(uint16_t block_size, uint32_t block_count)
{
	const uint16_t c = block_size / 4; // blocks per indirect block
	uint32_t b = block_count;
	// singly, doubly and triply-indirect blocks
	uint32_t s = 0, d = 0, t = 0;
	if (b > 12) {
		// We'll need a singly-indirect block for every c blocks
		s = CEIL_DIV(b - 12, c);
		if (s > 1) {
			// A doubly-indirect block for every c singly-indirect blocks
			d = CEIL_DIV(s - 1, c);
			if (d > 1) {
				// A triply-indirect block for every c doubly-indirect blocks
				t = CEIL_DIV(d - 1, c);
			}
		}
	}

	struct indirect_block_count_t bcnt = {
		.singly_indirect = s,
		.doubly_indirect = d,
		.triply_indirect = t,
		.total_indirect = s + d + t,
	};
	return bcnt;
}

void read_u32_from_block(int fd, struct fsinfo_t *fs, uint32_t block_id, uint16_t pos, uint32_t *value)
{
	uint64_t blocks_pos = fs->blocks_pos;
	uint16_t bsize = fs->main_block.block_size;
	lseek(fd, blocks_pos + block_id * (uint64_t)bsize + pos * 4, SEEK_SET);
	uint8_t buf[4];
	read(fd, buf, 4);
	util_read_u32(buf, value);
}

#include <stdio.h>
void write_u32_to_block(int fd, struct fsinfo_t *fs, uint32_t block_id, uint16_t pos, uint32_t value)
{
	uint64_t blocks_pos = fs->blocks_pos;
	uint16_t bsize = fs->main_block.block_size;
	lseek(fd, blocks_pos + block_id * (uint64_t)bsize + pos * 4, SEEK_SET);
	//printf("HELPER: Write %u to %lu\n", value, blocks_pos + block_id * (uint64_t)bsize + pos * 4);
	uint8_t buf[4];
	util_write_u32(buf, value);
	write(fd, buf, 4);
}
