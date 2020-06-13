#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "util.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void initialize_fsinfo(struct fsinfo_t *fs, uint64_t size)
{
	const uint16_t block_size = 4096;
	const uint64_t block_count = size / block_size;
	const uint32_t inode_count = block_count / 4;
	const uint32_t inode_bitmap_blocks = inode_count / block_size
		+ (inode_count % block_size != 0);
	const uint64_t reserved_block_count = 1 + inode_bitmap_blocks;
	const uint64_t usable_data_blocks = block_count - reserved_block_count;

	struct main_block_t mb = {
		.inode_count_limit = inode_count,
		.inode_count = 0,
		.block_count = block_count,
		.free_block_count = block_count - reserved_block_count,
		.reserved_block_count = reserved_block_count,
		.block_size = block_size,
	};

	fs->main_block = mb;
	fs->inode_bitmap_blocks = inode_bitmap_blocks;
	fs->inode_bitmap_pos = MAIN_BLOCK_SIZE;
	fs->data_blocks_bitmap_pos = fs->inode_bitmap_pos + fs->inode_bitmap_blocks * block_size;
	fs->inodes_pos = fs->data_blocks_bitmap_pos + usable_data_blocks * block_size;
	fs->blocks_pos = fs->inodes_pos + inode_count * INODE_SIZE;
}

void initialize_inode(struct inode_t *inode)
{
	struct inode_t i = {
		.ctime = 0,
		.mtime = 0,
		.size = 0,
		.blocks = 0,
		.uid = 0,
		.gid = 0,
		.mode = 0,
	};

	*inode = i;
}

void write_main_block(int fd, const struct fsinfo_t *fs)
{
	uint8_t buffer[MAIN_BLOCK_SIZE];
	uint8_t *b = buffer;
	util_writeseq_u32(&b, fs->main_block.inode_count_limit);
	util_writeseq_u32(&b, fs->main_block.inode_count);
	util_writeseq_u64(&b, fs->main_block.block_count);
	util_writeseq_u64(&b, fs->main_block.free_block_count);
	util_writeseq_u64(&b, fs->main_block.reserved_block_count);
	util_writeseq_u16(&b, fs->main_block.block_size);

	// TODO: error checking
	lseek(fd, 0, SEEK_SET);
	write(fd, buffer, sizeof(buffer));
}

void write_inode(int fd, const struct fsinfo_t *fs, uint32_t inode_num, const struct inode_t *inode)
{
	uint64_t pos = fs->inodes_pos;
	pos += (uint64_t)INODE_SIZE * inode_num;
	uint8_t buffer[INODE_SIZE];

	uint8_t *b = buffer;
	util_writeseq_u64(&b, inode->ctime);
	util_writeseq_u64(&b, inode->mtime);
	util_writeseq_u64(&b, inode->size);
	util_writeseq_u64(&b, inode->blocks);
	util_writeseq_u64(&b, inode->blockpos);
	util_writeseq_u32(&b, inode->uid);
	util_writeseq_u32(&b, inode->gid);
	util_writeseq_u16(&b, inode->mode);

	lseek(fd, pos, SEEK_SET);
	write(fd, buffer, INODE_SIZE);
}

void read_fsinfo(int fd, struct fsinfo_t *fs)
{
	uint8_t buffer[MAIN_BLOCK_SIZE];
	// TODO: error checking
	lseek(fd, 0, SEEK_SET);
	read(fd, buffer, sizeof(buffer));

	uint8_t *b = buffer;
	util_readseq_u32(&b, &fs->main_block.inode_count_limit);
	util_readseq_u32(&b, &fs->main_block.inode_count);
	util_readseq_u64(&b, &fs->main_block.block_count);
	util_readseq_u64(&b, &fs->main_block.free_block_count);
	util_readseq_u64(&b, &fs->main_block.reserved_block_count);
	util_readseq_u16(&b, &fs->main_block.block_size);

	const uint64_t usable_data_blocks = fs->main_block.block_count -
		fs->main_block.reserved_block_count;

	fs->inode_bitmap_blocks = fs->main_block.inode_count_limit / fs->main_block.block_size +
		(fs->main_block.inode_count_limit % fs->main_block.block_size != 0);
	fs->inode_bitmap_pos = MAIN_BLOCK_SIZE;
	fs->data_blocks_bitmap_pos = fs->inode_bitmap_pos +
		fs->inode_bitmap_blocks * fs->main_block.block_size;
	fs->inodes_pos = fs->data_blocks_bitmap_pos +
		usable_data_blocks * fs->main_block.block_size;
	fs->blocks_pos = fs->inodes_pos + fs->main_block.inode_count_limit * INODE_SIZE;
}

void read_inode(int fd, const struct fsinfo_t *fs, uint32_t inode_num, struct inode_t *inode)
{
	uint64_t pos = fs->inodes_pos;
	pos += (uint64_t)INODE_SIZE * inode_num;
	uint8_t buffer[INODE_SIZE];
	lseek(fd, pos, SEEK_SET);
	read(fd, buffer, INODE_SIZE);

	uint8_t *b = buffer;
	util_readseq_u64(&b, &inode->ctime);
	util_readseq_u64(&b, &inode->mtime);
	util_readseq_u64(&b, &inode->size);
	util_readseq_u64(&b, &inode->blocks);
	util_readseq_u64(&b, &inode->blockpos);
	util_readseq_u32(&b, &inode->uid);
	util_readseq_u32(&b, &inode->gid);
	util_readseq_u16(&b, &inode->mode);
}

void write_blank_data_bitmap(int fd, const struct fsinfo_t *fs)
{
	const uint32_t inode_count = fs->main_block.inode_count_limit;
	const uint16_t block_size = fs->main_block.block_size;
	const uint64_t block_count = fs->main_block.block_count;
	const uint32_t inode_bitmap_blocks = inode_count / block_size
		+ (inode_count % block_size != 0);
	uint8_t buffer[block_size];
	memset(buffer, 0x00, block_size);
	// TODO: error checking
	lseek(fd, MAIN_BLOCK_SIZE + inode_bitmap_blocks * block_size, SEEK_SET);
	for (uint64_t i = 1 + inode_bitmap_blocks; i < block_count; ++i)
		write(fd, buffer, block_size);
}

void write_blank_inode_bitmap(int fd, const struct fsinfo_t *fs)
{
	const uint32_t inode_count = fs->main_block.inode_count_limit;
	const uint16_t block_size = fs->main_block.block_size;
	const uint32_t inode_bitmap_blocks = inode_count / block_size
		+ (inode_count % block_size != 0);
	uint8_t buffer[block_size];
	memset(buffer, 0x00, block_size);
	// TODO: error checking
	lseek(fd, MAIN_BLOCK_SIZE, SEEK_SET);
	for (uint64_t i = 0; i < inode_bitmap_blocks; ++i)
		write(fd, buffer, block_size);
}

void write_blank_fs(int fd, struct fsinfo_t *fs)
{
	size_t size = lseek(fd, 0, SEEK_END);
	initialize_fsinfo(fs, size);

	write_main_block(fd, fs);
	write_blank_inode_bitmap(fd, fs);
	write_blank_data_bitmap(fd, fs);
}

void write_root_directory(int fd, struct fsinfo_t *fs)
{
}

void create_inode(int fd, struct fsinfo_t *fs, const struct inode_t *inode, uint32_t *inode_num)
{
	uint32_t ic = fs->main_block.inode_count_limit;
	uint32_t i;
	for (i = 0; i < ic; ++i)
		if (get_inode_state(fd, fs, i) == 0)
			break;
	if (i == ic) {
		// TODO
		exit(-1);
	}
	set_inode_state(fd, fs, i, 1);
	write_inode(fd, fs, i, inode);
	*inode_num = i;
}

uint8_t get_block_state(int fd, struct fsinfo_t *fs, uint32_t block)
{
	uint64_t pos = fs->data_blocks_bitmap_pos;
	pos += block / 8;
	uint8_t data;
	lseek(fd, pos, SEEK_SET);
	read(fd, &data, 1);
	return (data >> (block % 8)) & 1;
}

void set_block_state(int fd, struct fsinfo_t *fs, uint32_t block, uint8_t state)
{
	uint64_t pos = fs->data_blocks_bitmap_pos;
	pos += block / 8;
	uint8_t data;
	lseek(fd, pos, SEEK_SET);
	read(fd, &data, 1);
	if (state)
		data |= (1 << (block % 8));
	else
		data &= ~(1 << (block % 8));
	lseek(fd, pos, SEEK_SET);
	write(fd, &data, 1);
}

uint8_t get_inode_state(int fd, struct fsinfo_t *fs, uint32_t inode)
{
	uint64_t pos = fs->inode_bitmap_pos;
	pos += inode / 8;
	uint8_t data;
	lseek(fd, pos, SEEK_SET);
	read(fd, &data, 1);
	return (data >> (inode % 8)) & 1;
}

void set_inode_state(int fd, struct fsinfo_t *fs, uint32_t inode, uint8_t state)
{
	uint64_t pos = fs->inode_bitmap_pos;
	pos += inode / 8;
	uint8_t data;
	lseek(fd, pos, SEEK_SET);
	read(fd, &data, 1);
	if (state)
		data |= (1 << (inode % 8));
	else
		data &= ~(1 << (inode % 8));
	lseek(fd, pos, SEEK_SET);
	write(fd, &data, 1);
}

uint64_t inode_data_write(int fd, struct fsinfo_t *fs, uint32_t inode_num, struct inode_t *inode, const uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint64_t bsize = fs->main_block.block_size;
	const uint64_t block_count = fs->main_block.block_count - fs->main_block.reserved_block_count;

	uint64_t old_size = inode->size;
	uint64_t fsize = old_size + len;
	if (fsize > bsize) {
		// TODO: multiple blocks
		exit(-1);
	}

	// Allocate the required blocks
	uint64_t old_blocks = inode->blocks;
	uint64_t new_blocks = fsize / bsize + (fsize % bsize != 0);
	uint64_t alloc_bcnt = new_blocks - old_blocks;
	for (uint64_t i = 0; i < block_count && alloc_bcnt > 0; ++i) {
		if (!get_block_state(fd, fs, i)) {
			// TODO: multiple blocks
			inode->blockpos = i;
			++alloc_bcnt;
			break;
		}
	}
	inode->blocks = new_blocks;

	// Write the data
	uint64_t towrite = len;
	uint64_t written = 0;
	while (towrite > 0) {
		uint64_t b = towrite;
		if (b > bsize - pos % bsize)
			b = bsize - pos % bsize;
		uint64_t bpos = fs->blocks_pos + inode->blockpos * bsize; // TODO: multiple blocks
		lseek(fd, bpos, SEEK_SET);
		uint64_t w = write(fd, buffer + written, b);
		written += w;
		towrite -= w;
	}

	return written;
}

uint64_t inode_data_read(int fd, struct fsinfo_t *fs, uint32_t inode_num, struct inode_t *inode, uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint64_t bsize = fs->main_block.block_size;

	uint64_t fsize = inode->size;

	// Read the data
	uint64_t toread = len;
	if (toread < fsize - pos)
		toread = fsize - pos;
	uint64_t readb = 0;
	while (toread > 0) {
		uint64_t b = toread;
		if (b > bsize - pos % bsize)
			b = bsize - pos % bsize;
		uint64_t bpos = fs->blocks_pos + inode->blockpos * bsize; // TODO: multiple blocks
		lseek(fd, bpos, SEEK_SET);
		uint64_t r = read(fd, buffer + readb, b);
		if (r == 0)
			break;
		readb += r;
		toread -= r;
	}

	return readb;
}
