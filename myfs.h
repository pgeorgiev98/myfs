#ifndef MYFS_H_INCLUDED
#define MYFS_H_INCLUDED

#define _XOPEN_SOURCE 500

#include <stdint.h>

/*
 * Layout:
 * -------------------
 * Main block;
 * Inode bitmap;
 * Data blocks bitmap
 * Inode 0,
 * Inode 1,
 * ...
 * InodeN;
 * Block 0,
 * Block 1,
 * ...
 * BlockM;
 * -------------------
 */

#define MAIN_BLOCK_SIZE 34
#define INODE_SIZE 50

enum {
	ftype_dir = 0,
	ftype_file = 1,
};

struct main_block_t
{
	uint32_t inode_count_limit;
	uint32_t inode_count;
	uint64_t block_count;
	uint64_t free_block_count;
	uint64_t reserved_block_count;
	uint16_t block_size;
};

struct fsinfo_t
{
	struct main_block_t main_block;
	uint32_t inode_bitmap_blocks;
	uint64_t inode_bitmap_pos;
	uint64_t data_blocks_bitmap_pos;
	uint64_t inodes_pos;
	uint64_t blocks_pos;
};

struct inode_t
{
	uint64_t ctime;    /* Creation time */
	uint64_t mtime;    /* Modification time */
	uint64_t size;     /* The size of the file in bytes */
	uint64_t blocks;   /* Number of allocated blocks */
	uint64_t blockpos; /* Position of first block TODO */
	uint32_t uid;      /* User ID */
	uint32_t gid;      /* Group ID */
	uint16_t mode;     /* File mode (lower 9 bits) and type (upper 7 bits) */
};

void initialize_fsinfo(struct fsinfo_t *fs, uint64_t size);
void initialize_inode(struct inode_t *inode);

void write_main_block(int fd, const struct fsinfo_t *fs);
void write_inode(int fd, const struct fsinfo_t *fs, uint32_t inode_num, const struct inode_t *inode);

void read_fsinfo(int fd, struct fsinfo_t *fs);
void read_inode(int fd, const struct fsinfo_t *fs, uint32_t inode_num, struct inode_t *inode);

void write_blank_data_bitmap(int fd, const struct fsinfo_t *fs);
void write_blank_inode_bitmap(int fd, const struct fsinfo_t *fs);
void write_blank_fs(int fd, struct fsinfo_t *fs);

void write_root_directory(int fd, struct fsinfo_t *fs);

void create_inode(int fd, struct fsinfo_t *fs, const struct inode_t *inode, uint32_t *inode_num);

uint8_t get_inode_state(int fd, struct fsinfo_t *fs, uint32_t inode);
void set_inode_state(int fd, struct fsinfo_t *fs, uint32_t inode, uint8_t state);

#endif
