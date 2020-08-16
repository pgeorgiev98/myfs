#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "asserts.h"

#include "util.h"
#include "helpers.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void initialize_fsinfo(struct fsinfo_t *fs, uint64_t size)
{
	const uint16_t block_size = 4096;
	const uint32_t block_count = size / block_size;
	const uint32_t block_count_2 = block_count - 2; // Reserve space for 2 main blocks

	// Reserve space for the inode map and inode blocks
	const uint32_t inode_count = size / 4096;
	const uint32_t inode_map_block_count = CEIL_DIV(inode_count, 8 * block_size);
	const uint32_t inodes_block_count = CEIL_DIV(inode_count * INODE_SIZE, block_size);
	const uint32_t block_count_3 = block_count_2 - inode_map_block_count - inodes_block_count;

	// Reserve space for the data blocks and data block map
	const uint32_t data_block_count = (block_count_3 * 32) / 33;

	struct main_block_t mb = {
		.inode_count_limit = inode_count,
		.inode_count = 0,
		.block_count = block_count,
		.data_block_count = data_block_count,
		.free_data_block_count = data_block_count,
		.block_size = block_size,
	};

	initialize_fsinfo_from_main_block(fs, &mb);
}

void initialize_fsinfo_from_main_block(struct fsinfo_t *fs, const struct main_block_t *mb)
{
	const uint16_t bs = mb->block_size;

	const uint32_t inode_bitmap_blocks = CEIL_DIV(mb->inode_count_limit, (8 * bs));
	const uint32_t data_bitmap_blocks = CEIL_DIV(mb->data_block_count, (8 * bs));

	const uint64_t inode_bitmap_pos = MAIN_BLOCK_SIZE;
	const uint64_t data_blocks_bitmap_pos = inode_bitmap_pos + inode_bitmap_blocks * (uint64_t)bs;
	const uint64_t inodes_pos = data_blocks_bitmap_pos + data_bitmap_blocks * (uint64_t)bs;
	const uint64_t blocks_pos = inodes_pos + mb->inode_count_limit * (uint64_t)bs;

	fs->main_block = *mb;
	fs->inode_bitmap_blocks = inode_bitmap_blocks;
	fs->inode_bitmap_pos = inode_bitmap_pos;
	fs->data_blocks_bitmap_pos = data_blocks_bitmap_pos;
	fs->inodes_pos = inodes_pos;
	fs->blocks_pos = blocks_pos;
}

void initialize_inode(struct inode_t *inode, uint32_t uid, uint32_t gid, uint16_t mode)
{
	const time_t tm = time(NULL);

	struct inode_t i = {
		.ctime = tm,
		.mtime = tm,
		.size = 0,
		.uid = uid,
		.gid = gid,
		.mode = mode,
		.nlinks = 0,
		.blocks = 0,
	};

	*inode = i;
}

void clear_inode(struct inode_t *inode)
{
	struct inode_t i = {
		.ctime = 0,
		.mtime = 0,
		.size = 0,
		.uid = 0,
		.gid = 0,
		.mode = 0,
		.nlinks = 0,
		.blocks = 0,
	};

	*inode = i;
}

void write_main_block(int fd, const struct fsinfo_t *fs)
{
	uint8_t buffer[MAIN_BLOCK_SIZE];
	uint8_t *b = buffer;
	util_writeseq_u32(&b, fs->main_block.inode_count_limit);
	util_writeseq_u32(&b, fs->main_block.inode_count);
	util_writeseq_u32(&b, fs->main_block.block_count);
	util_writeseq_u32(&b, fs->main_block.data_block_count);
	util_writeseq_u32(&b, fs->main_block.free_data_block_count);
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
	util_writeseq_u32(&b, inode->blocks);
	for (int i = 0; i < INODE_BLKS; ++i)
		util_writeseq_u32(&b, inode->blockpos[i]);
	util_writeseq_u32(&b, inode->uid);
	util_writeseq_u32(&b, inode->gid);
	util_writeseq_u16(&b, inode->mode);
	util_writeseq_u16(&b, inode->nlinks);

	lseek(fd, pos, SEEK_SET);
	write(fd, buffer, INODE_SIZE);
}

void read_fsinfo(int fd, struct fsinfo_t *fs)
{
	uint8_t buffer[MAIN_BLOCK_SIZE];
	// TODO: error checking
	lseek(fd, 0, SEEK_SET);
	read(fd, buffer, sizeof(buffer));

	struct main_block_t mb;
	uint8_t *b = buffer;
	util_readseq_u32(&b, &mb.inode_count_limit);
	util_readseq_u32(&b, &mb.inode_count);
	util_readseq_u32(&b, &mb.block_count);
	util_readseq_u32(&b, &mb.data_block_count);
	util_readseq_u32(&b, &mb.free_data_block_count);
	util_readseq_u16(&b, &mb.block_size);

	initialize_fsinfo_from_main_block(fs, &mb);
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
	util_readseq_u32(&b, &inode->blocks);
	for (int i = 0; i < INODE_BLKS; ++i)
		util_readseq_u32(&b, &inode->blockpos[i]);
	util_readseq_u32(&b, &inode->uid);
	util_readseq_u32(&b, &inode->gid);
	util_readseq_u16(&b, &inode->mode);
	util_readseq_u16(&b, &inode->nlinks);
}

void write_blank_data_bitmap(int fd, const struct fsinfo_t *fs)
{
	const uint32_t inode_count = fs->main_block.inode_count_limit;
	const uint16_t block_size = fs->main_block.block_size;
	const uint32_t block_count = fs->main_block.block_count;
	const uint32_t inode_bitmap_blocks = inode_count / block_size
		+ (inode_count % block_size != 0);
	uint8_t buffer[block_size];
	memset(buffer, 0x00, block_size);
	// TODO: error checking
	lseek(fd, MAIN_BLOCK_SIZE + inode_bitmap_blocks * block_size, SEEK_SET);
	for (uint32_t i = 1 + inode_bitmap_blocks; i < block_count; ++i)
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
	for (uint32_t i = 0; i < inode_bitmap_blocks; ++i)
		write(fd, buffer, block_size);
}

void write_blank_fs(int fd, struct fsinfo_t *fs)
{
	size_t size = lseek(fd, 0, SEEK_END);
	initialize_fsinfo(fs, size);

	write_main_block(fd, fs);
	write_blank_inode_bitmap(fd, fs);
	write_blank_data_bitmap(fd, fs);
	write_root_directory(fd, fs);
}

void create_inode(int fd, struct fsinfo_t *fs, const struct inode_t *inode, uint32_t *inode_num)
{
	uint32_t ic = fs->main_block.inode_count_limit;
	uint32_t i;
	for (i = 0; i < ic; ++i)
		if (get_inode_state(fd, fs, i) == 0)
			break;
	EXPECT_S(i != ic, "Failed to find free inode\n"); // TODO

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

/* Allocate block_count blocks and write their IDs to out_blocks
 *
 * returns: number of blocks allocated; less than block_count if out of space
 */
static uint32_t allocate_blocks(int fd, struct fsinfo_t *fs, uint32_t block_count, uint32_t *out_blocks)
{
	uint32_t allocated = 0;

	const uint16_t bs = fs->main_block.block_size;
	const uint64_t bitmap_pos = fs->data_blocks_bitmap_pos;
	uint8_t buffer[bs];
	uint64_t pos = bitmap_pos;
	do {
		// Load a page
		lseek(fd, pos, SEEK_SET);
		uint64_t s = read(fd, buffer, bs);

		uint32_t first_updated = (uint32_t)(-1);
		uint32_t last_updated = first_updated - 1;

		// Traverse page
		for (uint64_t i = 0; allocated < block_count && i < s; ++i) {
			uint8_t b = buffer[i];
			if (b != 0xFF) {
				// Traverse byte
				for (int j = 0; allocated < block_count && j < 8; ++j) {
					if (!(b & (1 << j))) {
						out_blocks[allocated++] = (pos - bitmap_pos + i) * 8 + j;
						b |= (1 << j);
					}
				}
				if (first_updated > last_updated)
					first_updated = i;
				last_updated = i;
				buffer[i] = b;
			}
		}

		// Update bytes
		if (first_updated <= last_updated) {
			lseek(fd, pos + first_updated, SEEK_SET);
			write(fd, buffer + first_updated, last_updated - first_updated + 1);
		}

		pos += s;
	} while (allocated < block_count);

	fs->main_block.free_data_block_count -= allocated;

	return allocated;
}

static void release_blocks(int fd, struct fsinfo_t *fs, uint32_t *blocks, uint32_t block_count)
{
	if (block_count == 0)
		return;

	const uint16_t bs = fs->main_block.block_size;
	const uint64_t bitmap_pos = fs->data_blocks_bitmap_pos;
	uint8_t buffer[bs];
	uint32_t left, right;
	uint32_t released = 0;

	while (released < block_count) {
		left = right = (blocks[released] / 8);
		uint32_t i;
		for (i = released + 1; i < block_count; ++i) {
			uint32_t loc = (blocks[i] / 8);
			uint32_t new_left = MIN(left, loc);
			uint32_t new_right = MAX(right, loc);
			if (new_right - new_left + 1 > bs)
				break;
			left = new_left;
			right = new_right;
		}
		lseek(fd, bitmap_pos + left, SEEK_SET);
		read(fd, buffer, right - left + 1);
		lseek(fd, bitmap_pos + left, SEEK_SET);
		for (uint32_t j = released; j < i; ++j)
			buffer[blocks[j] / 8 - left] &= ~(1 << (blocks[j] % 8));
		write(fd, buffer, right - left + 1);
		released = i;
	}

	fs->main_block.free_data_block_count += block_count;
}

uint64_t inode_data_write(int fd, struct fsinfo_t *fs, struct inode_t *inode, const uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint32_t bsize = fs->main_block.block_size;
	//const uint32_t block_count = fs->main_block.data_block_count;

	if (pos + len > inode->size)
		resize_file(fd, fs, inode, pos + len);

	// Write the data
	{
		uint64_t total_towrite = len; // total number of bytes to write
		uint64_t total_written = 0; // total number of byets written
		const uint16_t c = bsize / 4; // blocks per indirect block
		uint64_t cur_pos = pos;
		while (cur_pos < pos + len) {
			uint32_t file_block_id = cur_pos / bsize;
			uint32_t block_id;
			if (file_block_id < 12) {
				// Dirrectly get the block id
				block_id = inode->blockpos[file_block_id];
			} else if (file_block_id < 12 + c) {
				// Get the block id from a singly-indirect block
				uint32_t b = inode->blockpos[12];
				read_u32_from_block(fd, fs, b, file_block_id - 12, &block_id);
			} else if (file_block_id < 12 + c + c*c) {
				// Get the block id from a doubly-indirect block
				uint32_t fb = file_block_id - 12 - c;

				uint32_t b1 = inode->blockpos[13];
				uint32_t off1 = fb / c;

				uint32_t b2;
				uint32_t off2 = fb % c;

				read_u32_from_block(fd, fs, b1, off1, &b2);
				read_u32_from_block(fd, fs, b2, off2, &block_id);
			} else {
				// Get the block id from a triply-indirect block
				uint32_t fb = file_block_id - 12 - c - c*c;

				uint32_t b1 = inode->blockpos[14];
				uint32_t off1 = fb / (c*c);

				uint32_t b2;
				uint32_t off2 = fb % (c*c) / c;

				uint32_t b3;
				uint32_t off3 = fb % c;

				read_u32_from_block(fd, fs, b1, off1, &b2);
				read_u32_from_block(fd, fs, b2, off2, &b3);
				read_u32_from_block(fd, fs, b3, off3, &block_id);
			}
			uint64_t p = cur_pos % bsize;
			uint64_t towrite = total_towrite;
			uint64_t written = 0;
			if (towrite > bsize - p)
				towrite = bsize - p;
			cur_pos += towrite;
			total_towrite -= towrite;
			while (towrite > 0) {
				lseek(fd, fs->blocks_pos + block_id * (uint64_t)bsize + p, SEEK_SET);
				uint64_t w = write(fd, buffer + total_written, towrite);
				towrite -= w;
				written += w;
				total_written += w;
			}
		}
		EXPECT_EQUAL(cur_pos, pos + len);
	}

	// TODO: error checking
	return len;
}

uint64_t inode_data_read(int fd, struct fsinfo_t *fs, struct inode_t *inode, uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint32_t bsize = fs->main_block.block_size;

	uint64_t fsize = inode->size;
	if (pos >= fsize)
		return 0;

	if (pos + len > fsize)
		len = fsize - pos;

	// TODO: This is duplicated from inode_data_write()
	// Read the data
	{
		uint64_t total_toread = len; // total number of bytes read
		uint64_t total_readb = 0; // total number of byets to read
		const uint16_t c = bsize / 4; // blocks per indirect block
		uint64_t cur_pos = pos;
		while (cur_pos < pos + len) {
			uint32_t file_block_id = cur_pos / bsize;
			uint32_t block_id;
			if (file_block_id < 12) {
				// Dirrectly get the block id
				block_id = inode->blockpos[file_block_id];
			} else if (file_block_id < 12 + c) {
				// Get the block id from a singly-indirect block
				uint32_t b = inode->blockpos[12];
				read_u32_from_block(fd, fs, b, file_block_id - 12, &block_id);
			} else if (file_block_id < 12 + c + c*c) {
				// Get the block id from a doubly-indirect block
				uint32_t fb = file_block_id - 12 - c;

				uint32_t b1 = inode->blockpos[13];
				uint32_t off1 = fb / c;

				uint32_t b2;
				uint32_t off2 = fb % c;

				read_u32_from_block(fd, fs, b1, off1, &b2);
				read_u32_from_block(fd, fs, b2, off2, &block_id);
			} else {
				// Get the block id from a triply-indirect block
				uint32_t fb = file_block_id - 12 - c - c*c;

				uint32_t b1 = inode->blockpos[14];
				uint32_t off1 = fb / (c*c);

				uint32_t b2;
				uint32_t off2 = fb % (c*c) / c;

				uint32_t b3;
				uint32_t off3 = fb % c;

				read_u32_from_block(fd, fs, b1, off1, &b2);
				read_u32_from_block(fd, fs, b2, off2, &b3);
				read_u32_from_block(fd, fs, b3, off3, &block_id);
			}
			uint64_t p = cur_pos % bsize;
			uint64_t toread = total_toread;
			uint64_t readb = 0;
			if (toread > bsize - p)
				toread = bsize - p;
			cur_pos += toread;
			total_toread -= toread;
			while (toread > 0) {
				lseek(fd, fs->blocks_pos + block_id * (uint64_t)bsize + p, SEEK_SET);
				uint64_t r = read(fd, buffer + total_readb, toread);
				toread -= r;
				readb += r;
				total_readb += r;
			}
		}
		EXPECT_EQUAL(cur_pos, pos + len);
	}

	return len;
}

void add_inode_to_dir(int fd, struct fsinfo_t *fs, uint32_t dir_inode_num, struct inode_t *dir_inode, uint32_t entry_inode_num, struct inode_t *entry_inode, const char *entry_name)
{
	uint32_t entries_count = 0;
	uint16_t starting_pos = 0;
	if (dir_inode->size > 0) {
		uint8_t header_buf[6];
		inode_data_read(fd, fs, dir_inode, header_buf, sizeof(header_buf), 0);
		util_read_u32(header_buf, &entries_count);
		util_read_u16(header_buf + 0x4, &starting_pos);
	}

	uint16_t name_len = strlen(entry_name);
	uint16_t padding = 32;
	uint16_t entry_len = name_len + padding + 10;

	uint8_t buffer[MAX_FILE_NAME_LENGTH + 10];
	util_write_u32(buffer + 0x0, entry_inode_num);
	util_write_u16(buffer + 0x4, entry_len);
	util_write_u16(buffer + entry_len - 0x2, entry_len);
	util_write_u16(buffer + 0x6, name_len);
	memcpy(buffer + 0x8, entry_name, name_len);

	{
		// Write the directory header
		uint8_t header_buf[6];
		util_write_u32(header_buf, entries_count + 1);
		util_write_u16(header_buf + 0x4, starting_pos);
		inode_data_write(fd, fs, dir_inode, header_buf, sizeof(header_buf), 0);
	}

	// Write the new entry to the directory
	inode_data_write(fd, fs, dir_inode, buffer, entry_len, dir_inode->size);

	// Update the directory inode
	write_inode(fd, fs, dir_inode_num, dir_inode);

	++entry_inode->nlinks;
	write_inode(fd, fs, entry_inode_num, entry_inode);
}

int remove_inode_from_dir(int fd, struct fsinfo_t *fs, struct inode_t *dir_inode, uint32_t entry_inode_num, struct inode_t *entry_inode)
{
	uint32_t entries_count = 0;
	uint16_t starting_pos = 0;
	if (dir_inode->size > 0) {
		uint8_t header_buf[6];
		inode_data_read(fd, fs, dir_inode, header_buf, sizeof(header_buf), 0);
		util_read_u32(header_buf, &entries_count);
		util_read_u16(header_buf + 0x4, &starting_pos);
	}

	const uint32_t buffer_len = MAX_FILE_NAME_LENGTH + 10;
	uint8_t buffer[buffer_len];
	uint64_t pos = starting_pos + 0x6;

	// Find entry_inode_num in directory
	int entry_found = 0;
	uint16_t cur_entry_len;
	uint32_t buffer_pos = buffer_len;
	uint32_t entry_index = 0;
	for (; entry_index < entries_count; ++entry_index) {
		if (buffer_pos + 0x6 >= buffer_len) {
			inode_data_read(fd, fs, dir_inode, buffer, buffer_len, pos);
			buffer_pos = 0;
		}

		uint32_t cur_inode_num;
		util_read_u32(buffer + buffer_pos, &cur_inode_num);
		util_read_u16(buffer + buffer_pos + 0x4, &cur_entry_len);

		if (cur_inode_num == entry_inode_num) {
			entry_found = 1;
			break;
		}

		pos += cur_entry_len;
		buffer_pos += cur_entry_len;
	}

	if (!entry_found)
		return 0;

	if (entries_count == 1) {
		// If no more entries are left just empty the directory
		resize_file(fd, fs, dir_inode, 0);

	} else if (entry_index == entries_count - 1) {
		// If we have to remove the last entry, just resize the directory
		resize_file(fd, fs, dir_inode, pos);

	} else {
		uint16_t last_entry_len;
		inode_data_read(fd, fs, dir_inode, buffer, 2, dir_inode->size - 2);
		util_read_u16(buffer, &last_entry_len);

		if (entry_index == 0) {
			if (starting_pos + cur_entry_len >= last_entry_len) {
				// Move the last entry to the begining

				inode_data_read(fd, fs, dir_inode, buffer, last_entry_len,
						dir_inode->size - last_entry_len);

				// Extend the entry to fill the space to the next entry
				uint16_t new_entry_len = starting_pos + cur_entry_len;
				util_write_u16(buffer + 0x4, new_entry_len);
				util_write_u16(buffer + last_entry_len - 0x2, new_entry_len);

				inode_data_write(fd, fs, dir_inode, buffer, new_entry_len, 0x6);

				// Resize the directory
				resize_file(fd, fs, dir_inode, dir_inode->size - last_entry_len);

				starting_pos = 0;

			} else {
				starting_pos += cur_entry_len;
			}

			// Update `starting_pos`
			util_write_u16(buffer, starting_pos);
			inode_data_write(fd, fs, dir_inode, buffer, 2, 0x4);

		} else {
			// Get the size of the previous entry
			uint16_t prev_entry_len;
			inode_data_read(fd, fs, dir_inode, buffer, 2, pos - 0x2);
			util_read_u16(buffer, &prev_entry_len);

			uint64_t prev_entry_pos = pos - prev_entry_len;
			uint16_t prev_entry_name_len;
			inode_data_read(fd, fs, dir_inode, buffer, 2, prev_entry_pos + 0x6);
			util_read_u16(buffer, &prev_entry_name_len);

			uint16_t prev_padding = prev_entry_len - prev_entry_name_len - 0x8;

			if (prev_padding + cur_entry_len >= last_entry_len + 32) { // TODO: do we need that 32?
				uint32_t in;
				uint8_t bu[4];
				inode_data_read(fd, fs, dir_inode, buffer, 4, dir_inode->size - last_entry_len);
				util_read_u32(bu, &in);
				// Move the last entry
				inode_data_read(fd, fs, dir_inode, buffer, last_entry_len,
						dir_inode->size - last_entry_len);
				inode_data_write(fd, fs, dir_inode, buffer, last_entry_len,
						pos + cur_entry_len - last_entry_len);

				// Resize the directory
				resize_file(fd, fs, dir_inode, dir_inode->size - last_entry_len);

				// Extend the previous entry
				uint16_t new_len = prev_entry_len + cur_entry_len - last_entry_len;
				util_write_u16(buffer, new_len);
				inode_data_write(fd, fs, dir_inode, buffer, 2, prev_entry_pos + 0x4);
				inode_data_write(fd, fs, dir_inode, buffer, 2, prev_entry_pos + new_len - 0x2);

			} else {
				// Extend the previous entry
				uint16_t new_len = prev_entry_len + cur_entry_len;
				util_write_u16(buffer, new_len);
				inode_data_write(fd, fs, dir_inode, buffer, 2, prev_entry_pos + 0x4);
				inode_data_write(fd, fs, dir_inode, buffer, 2, prev_entry_pos + new_len - 0x2);
			}
		}
	}

	// Update `entries_count`
	if (dir_inode->size > 0) {
		util_write_u32(buffer, entries_count - 1);
		inode_data_write(fd, fs, dir_inode, buffer, 4, 0);
	}

	// Remove the file if no more hard links remain
	if (--entry_inode->nlinks == 0)
		remove_file(fd, fs, entry_inode_num, entry_inode);
	else
		write_inode(fd, fs, entry_inode_num, entry_inode);

	return 1;
}

void write_root_directory(int fd, struct fsinfo_t *fs)
{
	struct inode_t root_inode;
	initialize_inode(&root_inode, 0, 0, 0755 | mode_ftype_dir);
	set_inode_state(fd, fs, 0, 1);
	write_inode(fd, fs, 0, &root_inode);
}

int get_path_inode(int fd, struct fsinfo_t *fs, const char *path, uint32_t *inode_num, struct inode_t *inode, uint32_t *dir_inode_num, struct inode_t *dir_inode, uint64_t *offset)
{
	if (path[0] != '/')
		return 0;

	if (!strcmp(path, "/")) {
		*inode_num = 0;
		read_inode(fd, fs, 0, inode);
		return 1;
	}

	uint32_t prev_inode_num = 0;
	struct inode_t prev_inode;
	uint32_t cur_inode_num = 0;
	struct inode_t cur_inode;
	read_inode(fd, fs, cur_inode_num, &cur_inode);

	// Parse `path`
	int fname_begin = 1;
	for (int fname_end = 1; ; ++fname_end) {
		char c = path[fname_end];
		if (c == '\0' || c == '/') {
			// Search for a file named `path[fname_begin:fname_end]` in the current inode
			prev_inode_num = cur_inode_num;
			prev_inode = cur_inode;
			uint8_t buffer[fs->main_block.block_size];
			uint64_t s = inode_data_read(fd, fs, &cur_inode, buffer, sizeof(buffer), 0);
			if (s == 0)
				return 0;
			uint32_t inodes_count;
			uint16_t starting_pos;
			util_read_u32(buffer, &inodes_count);
			util_read_u16(buffer + 0x4, &starting_pos);

			uint64_t file_pos = 0;
			uint64_t pos = starting_pos + 0x6;
			int inode_found = 0;
			for (uint32_t i = 0; i < inodes_count; ++i) {
				uint16_t entry_len;
				uint16_t name_len;
				EXPECT(pos < s); // TODO: Error handling

				// Load next page if we're at the end of the buffer
				if (pos + 8 > s) {
					file_pos += pos;
					pos = 0;
					s = inode_data_read(fd, fs, &cur_inode, buffer, sizeof(buffer), file_pos);
				}

				// Read entry header
				util_read_u32(buffer + pos, &cur_inode_num);
				util_read_u16(buffer + pos + 0x4, &entry_len);
				util_read_u16(buffer + pos + 0x6, &name_len);
				EXPECT(name_len <= MAX_FILE_NAME_LENGTH); // TODO: error handling

				// Load next page if we're at the end of the buffer
				if (pos + 8 + name_len > s) {
					file_pos += pos;
					pos = 0;
					s = inode_data_read(fd, fs, &cur_inode, buffer, sizeof(buffer), file_pos);
				}
				EXPECT(pos + 8 + name_len <= s);

				if (name_len == fname_end - fname_begin &&
						strncmp((const char *)(buffer + pos + 0x8), path + fname_begin, name_len) == 0) {
					read_inode(fd, fs, cur_inode_num, &cur_inode);
					if (offset)
						*offset = pos;
					inode_found = 1;
					break;
				}
				pos += entry_len;
			}

			if (!inode_found)
				return 0;

			fname_begin = fname_end + 1;

			if (c == '\0')
				break;
		}
	}

	if (inode_num != 0) {
		if (dir_inode_num)
			*dir_inode_num = prev_inode_num;
		if (dir_inode)
			*dir_inode = prev_inode;
	}
	*inode_num = cur_inode_num;
	*inode = cur_inode;
	return 1;
}

void resize_file(int fd, struct fsinfo_t *fs, struct inode_t *inode, uint64_t size)
{
	// TODO: check max file size

	const uint32_t bsize = fs->main_block.block_size;
	const uint32_t old_blocks = inode->blocks;
	const uint32_t new_blocks = CEIL_DIV(size, bsize);

	const struct indirect_block_count_t old_indirect_bcnt =
		calc_indirect_block_count(bsize, old_blocks);
	const struct indirect_block_count_t new_indirect_bcnt =
		calc_indirect_block_count(bsize, new_blocks);

	if (new_blocks > old_blocks) {
		// Allocate the required blocks

		// Number of blocks to alloc including indirect blocks
		const uint32_t blocks_to_alloc =
			new_indirect_bcnt.total_indirect - old_indirect_bcnt.total_indirect +
			new_blocks - old_blocks;

		// TODO: try not to call malloc when possible
		uint32_t *blocks = (uint32_t *)malloc(blocks_to_alloc * sizeof(uint32_t));
		EXPECT_EQUAL(allocate_blocks(fd, fs, blocks_to_alloc, blocks), blocks_to_alloc);
		const uint32_t indirect_blocks_allocated =
			new_indirect_bcnt.total_indirect - old_indirect_bcnt.total_indirect;
		const uint32_t *indirect_blocks = blocks;
		const uint32_t *data_blocks = blocks + indirect_blocks_allocated;

		// Initialize the new indirect blocks
		const uint16_t c = bsize / 4; // blocks per indirect block
		uint32_t ibptr = 0; // indirect block pointer
		uint32_t dbptr = 0; // direct block pointer
		for (uint32_t cur_block = old_blocks; cur_block < new_blocks; ++cur_block) {
			uint32_t b = cur_block;
			if (b < 12) {
				// Set pointer to direct block
				inode->blockpos[b] = data_blocks[dbptr++];
			} else if (b < 12 + c) {
				b -= 12;

				uint32_t b1;
				uint32_t off1 = b;

				// If needed, set the pointer to the singly-indirect block
				if (b == 0)
					inode->blockpos[12] = indirect_blocks[ibptr++];
				b1 = inode->blockpos[12];

				// Set the pointer to the block
				write_u32_to_block(fd, fs, b1, off1, data_blocks[dbptr++]);
			} else if (b < 12 + c + c*c) {
				b -= 12 + c;

				uint32_t b1;
				uint32_t off1 = b / c;

				uint32_t b2;
				uint32_t off2 = b % c;

				// If needed, set the pointer to the doubly-indirect block
				if (b == 0)
					inode->blockpos[13] = indirect_blocks[ibptr++];
				b1 = inode->blockpos[13];

				// If needed, set the pointer to the singly-indirect block
				if (off2 == 0) {
					b2 = indirect_blocks[ibptr++];
					write_u32_to_block(fd, fs, b1, off1, b2);
				} else {
					read_u32_from_block(fd, fs, b1, off1, &b2);
				}
				// Set the pointer to the block
				write_u32_to_block(fd, fs, b2, off2, data_blocks[dbptr++]);
			} else {
				b -= 12 + c + c*c;

				uint32_t b1;
				uint32_t off1 = b / (c*c);

				uint32_t b2;
				uint32_t off2 = (b % (c*c)) / c;

				uint32_t b3;
				uint32_t off3 = b % c;

				// If needed, set the pointer to the triply-indirect block
				if (b == 0)
					inode->blockpos[14] = indirect_blocks[ibptr++];
				b1 = inode->blockpos[14];

				// If needed, set the pointer to the doubly-indirect block
				if (b % (c*c) == 0) {
					b2 = indirect_blocks[ibptr++];
					write_u32_to_block(fd, fs, b1, off1, b2);
				} else {
					read_u32_from_block(fd, fs, b1, off1, &b2);
				}

				// If needed, set the pointer to the singly-indirect block
				if (b % c == 0) {
					b3 = indirect_blocks[ibptr++];
					write_u32_to_block(fd, fs, b2, off2, b3);
				} else {
					read_u32_from_block(fd, fs, b2, off2, &b3);
				}

				// Set the pointer to the block
				write_u32_to_block(fd, fs, b3, off3, data_blocks[dbptr++]);
			}
		}
		EXPECT_EQUAL(ibptr, indirect_blocks_allocated);
		EXPECT_EQUAL(dbptr, new_blocks - old_blocks);

		free(blocks);

	} else if (new_blocks < old_blocks) {
		// Number of blocks to release including indirect blocks
		const uint32_t blocks_to_release =
			old_indirect_bcnt.total_indirect - new_indirect_bcnt.total_indirect +
			old_blocks - new_blocks;

		// TODO: try not to call malloc when possible
		uint32_t *blocks = (uint32_t *)malloc(blocks_to_release * sizeof(uint32_t));
		uint32_t bptr = 0;


		// Initialize the new indirect blocks
		const uint16_t c = bsize / 4; // blocks per indirect block
		for (uint32_t cur_block = old_blocks; cur_block-- > new_blocks; ) {
			uint32_t b = cur_block;
			if (b < 12) {
				// Release direct block
				blocks[bptr++] = inode->blockpos[b];
			} else if (b < 12 + c) {
				b -= 12;

				uint32_t b1 = inode->blockpos[12];
				uint32_t off1 = b;

				// If needed, release the singly-indirect block
				if (b == 0)
					blocks[bptr++] = b1;

				// Release the direct block
				read_u32_from_block(fd, fs, b1, off1, &blocks[bptr++]);
			} else if (b < 12 + c + c*c) {
				b -= 12 + c;

				uint32_t b1 = inode->blockpos[13];
				uint32_t off1 = b / c;

				uint32_t b2;
				uint32_t off2 = b % c;
				read_u32_from_block(fd, fs, b1, off1, &b2);

				// If needed, release the doubly-indirect block
				if (b == 0)
					blocks[bptr++] = b1;

				// If needed, release the singly-indirect block
				if (off2 == 0)
					blocks[bptr++] = b2;

				// Release the direct block
				read_u32_from_block(fd, fs, b2, off2, &blocks[bptr++]);
			} else {
				b -= 12 + c + c*c;

				uint32_t b1 = inode->blockpos[14];
				uint32_t off1 = b / (c*c);

				uint32_t b2;
				uint32_t off2 = (b % (c*c)) / c;
				read_u32_from_block(fd, fs, b1, off1, &b2);

				uint32_t b3;
				uint32_t off3 = b % c;
				read_u32_from_block(fd, fs, b2, off2, &b3);

				// If needed, release the triply-indirect block
				if (b == 0)
					blocks[bptr++] = b1;

				// If needed, release the doubly-indirect block
				if (b % (c*c) == 0)
					blocks[bptr++] = b2;

				// If needed, release the singly-indirect block
				if (b % c == 0)
					blocks[bptr++] = b3;

				// Release the direct block
				read_u32_from_block(fd, fs, b3, off3, &blocks[bptr++]);
			}
		}
		EXPECT_EQUAL(bptr, blocks_to_release);

		release_blocks(fd, fs, blocks, bptr);

		free(blocks);
	}

	inode->size = size;
	inode->blocks = new_blocks;
}

void remove_file(int fd, struct fsinfo_t *fs, uint32_t inode_num, struct inode_t *inode)
{
	EXPECT(inode->nlinks == 0);

	// This should free up all blocks
	resize_file(fd, fs, inode, 0);

	set_inode_state(fd, fs, inode_num, 0);
}
