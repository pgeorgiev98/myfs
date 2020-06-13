#define _XOPEN_SOURCE 500

#include "myfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define EXPECT(what, name...) if (!(what)) printf("FAILED: " name);
#define CMP_STRUCT(a, b) memcmp2(&a, &b, sizeof(a))

static uint8_t memcmp2(const void *a, const void *b, uint32_t len)
{
	for (uint32_t i = 0; i < len; ++i)
		if (((const uint8_t *)a)[i] != ((const uint8_t *)b)[i])
			return 0;
	return 1;
}

static int fd = -1;
static const char *path = NULL;
static struct fsinfo_t fs;

static void create_fs(uint64_t size)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		perror("Failed to create device");
		exit(1);
	}
	ftruncate(fd, size);

	struct fsinfo_t fs;
	write_blank_fs(fd, &fs);

	close(fd);
}

static void print_fs_info(void)
{
	printf(
			"Max number of inodes:      %u\n"
			"Number of inodes:          %u\n"
			"Total number of blocks:    %lu\n"
			"Number of free blocks:     %lu\n"
			"Number of reserved blocks: %lu\n"
			"Block size:                %hu\n"
			, fs.main_block.inode_count_limit
			, fs.main_block.inode_count
			, fs.main_block.block_count
			, fs.main_block.free_block_count
			, fs.main_block.reserved_block_count
			, fs.main_block.block_size
		  );
}

static void test_inode_state()
{
	for (int i = 0; i < 10; ++i) {
		set_inode_state(fd, &fs, i, 0);
		EXPECT(get_inode_state(fd, &fs, i) == 0, "Inode state X to 0");
		set_inode_state(fd, &fs, i, 0);
		EXPECT(get_inode_state(fd, &fs, i) == 0, "Inode state 0 to 0");
		set_inode_state(fd, &fs, i, 1);
		EXPECT(get_inode_state(fd, &fs, i) == 1, "Inode state 0 to 1");
		set_inode_state(fd, &fs, i, 1);
		EXPECT(get_inode_state(fd, &fs, i) == 1, "Inode state 1 to 1");
		set_inode_state(fd, &fs, i, 0);
		EXPECT(get_inode_state(fd, &fs, i) == 0, "Inode state 1 to 0");
	}

	int inodes[] = { 0,  5,  2,  7,  3, 30, 29, 31,  2, 30,  0, 16, 19, 30};
	int states[] = { 1,  0,  1,  0,  1,  1,  0,  0,  0,  1,  1,  0,  1,  1};
	int count = sizeof(inodes) / sizeof(int);
	uint8_t s[fs.main_block.inode_count_limit];
	for (int i = 0; i < fs.main_block.inode_count_limit; ++i)
		s[i] = get_inode_state(fd, &fs, i);
	for (int i = 0; i < count; ++i) {
		set_inode_state(fd, &fs, inodes[i], states[i]);
		s[inodes[i]] = states[i];
	}
	for (int i = 0; i < fs.main_block.inode_count_limit; ++i)
		EXPECT(get_inode_state(fd, &fs, i) == s[i], "Verify inode state %d\n", i);
}

static uint32_t count_inodes(void)
{
	const uint32_t s = fs.main_block.inode_count_limit;
	uint32_t c = 0;
	for (int i = 0; i < s; ++i)
		c += get_inode_state(fd, &fs, i);
	return c;
}

static void test_inode_create(void)
{
	uint32_t ic = fs.main_block.inode_count_limit;
	uint32_t old_count = count_inodes();
	struct inode_t inodes[10];
	uint32_t numbers[10];
	for (int i = 0; i < 10; ++i) {
		const struct inode_t in = {
			.ctime = i + 10,
			.mtime = i + 10,
			.size = 0,
			.blocks = 0,
			.uid = i,
			.gid = i * 10,
			.mode = i % 2,
		};
		inodes[i] = in;
		uint32_t inode_num;
		create_inode(fd, &fs, &in, &inode_num);
		EXPECT(inode_num < ic, "create_inode() returned illegal inode number %d\n", inode_num);
		numbers[i] = inode_num;
	}
	for (int i = 0; i < 10; ++i) {
		struct inode_t in;
		read_inode(fd, &fs, numbers[i], &in);
		EXPECT(CMP_STRUCT(in, inodes[i]), "Inodes are different\n");
	}
	uint32_t new_count = count_inodes();
	EXPECT(old_count + 10 == new_count, "Wrong inode count. Expected %d, actual: %d\n", old_count + 10, new_count);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 1;
	}

	path = argv[1];

	create_fs(1024*1024);

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Failed to open device");
		exit(1);
	}

	read_fsinfo(fd, &fs);

	print_fs_info();

	test_inode_state();

	test_inode_create();

	return 0;
}
