#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "asserts.h"

#include "util.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// ceil(A/B)
#define CEIL_DIV(A, B) ((A)/(B) + ((A)%(B) != 0))

void initialize_fsinfo(struct fsinfo_t *fs, uint64_t size)
{
	const uint16_t block_size = 4096;
	const uint64_t block_count = size / block_size;
	const uint64_t block_count_2 = block_count - 2; // Reserve space for 2 main blocks

	// Reserve space for the inode map and inode blocks
	const uint32_t inode_count = size / 4096;
	const uint32_t inode_map_block_count = CEIL_DIV(inode_count, 8 * block_size);
	const uint32_t inodes_block_count = CEIL_DIV(inode_count * INODE_SIZE, block_size);
	const uint64_t block_count_3 = block_count_2 - inode_map_block_count - inodes_block_count;

	// Reserve space for the data blocks and data block map
	const uint64_t data_block_count = (block_count_3 * 32) / 33;

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

	const uint64_t inode_bitmap_blocks = CEIL_DIV(mb->inode_count_limit, (8 * bs));
	const uint64_t data_bitmap_blocks = CEIL_DIV(mb->data_block_count, (8 * bs));

	const uint64_t inode_bitmap_pos = MAIN_BLOCK_SIZE;
	const uint64_t data_blocks_bitmap_pos = inode_bitmap_pos + inode_bitmap_blocks * bs;
	const uint64_t inodes_pos = data_blocks_bitmap_pos + data_bitmap_blocks * bs;
	const uint64_t blocks_pos = inodes_pos + mb->inode_count_limit * bs;

	fs->main_block = *mb;
	fs->inode_bitmap_blocks = inode_bitmap_blocks;
	fs->inode_bitmap_pos = inode_bitmap_pos;
	fs->data_blocks_bitmap_pos = data_blocks_bitmap_pos;
	fs->inodes_pos = inodes_pos;
	fs->blocks_pos = blocks_pos;
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
	util_writeseq_u64(&b, fs->main_block.data_block_count);
	util_writeseq_u64(&b, fs->main_block.free_data_block_count);
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

	struct main_block_t mb;
	uint8_t *b = buffer;
	util_readseq_u32(&b, &mb.inode_count_limit);
	util_readseq_u32(&b, &mb.inode_count);
	util_readseq_u64(&b, &mb.block_count);
	util_readseq_u64(&b, &mb.data_block_count);
	util_readseq_u64(&b, &mb.free_data_block_count);
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

uint64_t inode_data_write(int fd, struct fsinfo_t *fs, struct inode_t *inode, const uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint64_t bsize = fs->main_block.block_size;
	const uint64_t block_count = fs->main_block.data_block_count;

	uint64_t old_size = inode->size;
	uint64_t fsize = old_size;
	if (pos + len > fsize)
		fsize = pos + len;

	EXPECT_S(fsize <= bsize, "File too large\n");

	// Allocate the required blocks
	uint64_t old_blocks = inode->blocks;
	uint64_t new_blocks = fsize / bsize + (fsize % bsize != 0);
	uint64_t alloc_bcnt = new_blocks - old_blocks;
	for (uint64_t i = 0; i < block_count && alloc_bcnt > 0; ++i) {
		if (!get_block_state(fd, fs, i)) {
			// TODO: multiple blocks
			set_block_state(fd, fs, i, 1);
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
		uint64_t bpos = fs->blocks_pos + inode->blockpos * bsize + pos; // TODO: multiple blocks
		lseek(fd, bpos, SEEK_SET);
		uint64_t w = write(fd, buffer + written, b);
		written += w;
		towrite -= w;
	}

	inode->size += written;
	inode->size = fsize;

	return written;
}

uint64_t inode_data_read(int fd, struct fsinfo_t *fs, struct inode_t *inode, uint8_t *buffer, uint64_t len, uint64_t pos)
{
	if (len == 0)
		return 0;

	const uint64_t bsize = fs->main_block.block_size;

	uint64_t fsize = inode->size;
	if (pos >= fsize)
		return 0;

	// Read the data
	uint64_t toread = len;
	if (toread > fsize - pos)
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

void add_inode_to_dir(int fd, struct fsinfo_t *fs, uint32_t dir_inode_num, struct inode_t *dir_inode, uint32_t entry_inode, const char *entry_name)
{
	uint64_t entries_count = 0;
	if (dir_inode->size > 0) {
		uint8_t header_buf[8];
		inode_data_read(fd, fs, dir_inode, header_buf, sizeof(header_buf), 0);
		util_read_u64(header_buf, &entries_count);
	}

	uint16_t name_len = strlen(entry_name);
	uint8_t buffer[512 + 6]; // TODO
	uint64_t buffer_len = 4 + 2 + name_len;
	util_write_u32(buffer + 0x0, entry_inode);
	util_write_u16(buffer + 0x4, name_len);
	memcpy(buffer + 0x6, entry_name, name_len);

	{
		// Write the directory header
		uint8_t header_buf[8];
		util_write_u64(header_buf, entries_count + 1);
		inode_data_write(fd, fs, dir_inode, header_buf, sizeof(header_buf), 0);
	}

	// Write the new entry to the directory
	inode_data_write(fd, fs, dir_inode, buffer, buffer_len, dir_inode->size);

	// Update the directory inode
	write_inode(fd, fs, dir_inode_num, dir_inode);
}

void write_root_directory(int fd, struct fsinfo_t *fs)
{
	struct inode_t root_inode;
	initialize_inode(&root_inode);
	// TODO: Set file mode
	set_inode_state(fd, fs, 0, 1);
	write_inode(fd, fs, 0, &root_inode);
}

int get_path_inode(int fd, struct fsinfo_t *fs, const char *path, uint32_t *inode_num, struct inode_t *inode)
{
	if (path[0] != '/')
		return 0;

	if (!strcmp(path, "/")) {
		*inode_num = 0;
		read_inode(fd, fs, 0, inode);
		return 1;
	}

	uint32_t cur_inode_num = 0;
	struct inode_t cur_inode;
	read_inode(fd, fs, cur_inode_num, &cur_inode);

	// Parse `path`
	int fname_begin = 1;
	for (int fname_end = 1; ; ++fname_end) {
		char c = path[fname_end];
		if (c == '\0' || c == '/') {
			// Search for a file named `path[fname_begin:fname_end]` in the current inode
			uint8_t buffer[fs->main_block.block_size];
			uint64_t s = inode_data_read(fd, fs, &cur_inode, buffer, sizeof(buffer), 0);
			if (s == 0)
				return 0;
			uint64_t pos = 8;
			uint64_t inodes_count;
			util_read_u64(buffer, &inodes_count);
			int inode_found = 0;

			for (uint64_t i = 0; i < inodes_count; ++i) {
				uint16_t name_len;
				EXPECT(pos + 6 <= s);
				util_read_u32(buffer + pos, &cur_inode_num);
				util_read_u16(buffer + pos + 4, &name_len);
				EXPECT(pos + 6 + name_len <= s);
				if (name_len == fname_end - fname_begin &&
						strncmp((const char *)(buffer + pos + 6), path + fname_begin, name_len) == 0) {
					read_inode(fd, fs, cur_inode_num, &cur_inode);
					inode_found = 1;
					break;
				}
				pos += 6 + name_len;
			}

			if (!inode_found)
				return 0;

			fname_begin = fname_end + 1;

			if (c == '\0')
				break;
		}
	}

	*inode_num = cur_inode_num;
	*inode = cur_inode;
	return 1;
}
