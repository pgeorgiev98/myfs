#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "asserts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

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
static uint64_t file_size = 0;

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

	file_size = size;
}

static void print_fs_info(void)
{
	uint64_t wasted_bytes = file_size - fs.main_block.data_block_count * fs.main_block.block_size;
	printf(
			"Max number of inodes:      %u\n"
			"Number of inodes:          %u\n"
			"Total number of blocks:    %lu\n"
			"Number of usable blocks:   %lu\n"
			"Number of free blocks:     %lu\n"
			"Block size:                %hu\n"
			"Reserved space:            %.2f%%\n"
			, fs.main_block.inode_count_limit
			, fs.main_block.inode_count
			, fs.main_block.block_count
			, fs.main_block.data_block_count
			, fs.main_block.free_data_block_count
			, fs.main_block.block_size
			, (100.0f * wasted_bytes) / file_size
		  );
}

static void test_inode_state()
{
	for (int i = 0; i < 10; ++i) {
		set_inode_state(fd, &fs, i, 0);
		EXPECT_EQUAL(get_inode_state(fd, &fs, i), 0);
		set_inode_state(fd, &fs, i, 0);
		EXPECT_EQUAL(get_inode_state(fd, &fs, i), 0);
		set_inode_state(fd, &fs, i, 1);
		EXPECT_EQUAL(get_inode_state(fd, &fs, i), 1);
		set_inode_state(fd, &fs, i, 1);
		EXPECT_EQUAL(get_inode_state(fd, &fs, i), 1);
		set_inode_state(fd, &fs, i, 0);
		EXPECT_EQUAL(get_inode_state(fd, &fs, i), 0);
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
		EXPECT_S(get_inode_state(fd, &fs, i) == s[i], "Inode %d was at wrong state", i);
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
		EXPECT_S(inode_num < ic, "create_inode() returned illegal inode number %d", inode_num);
		numbers[i] = inode_num;
	}
	for (int i = 0; i < 10; ++i) {
		struct inode_t in;
		read_inode(fd, &fs, numbers[i], &in);
		EXPECT_S(CMP_STRUCT(in, inodes[i]), "Inodes are different");
	}
	uint32_t new_count = count_inodes();
	EXPECT_EQUAL(old_count + 10, new_count);
}

static void test_inode_read_write(void)
{
	struct inode_t in;
	initialize_inode(&in);
	uint32_t inode_num;
	create_inode(fd, &fs, &in, &inode_num);
	const char outbuf[] = "Hello, world!";
	uint64_t len = strlen(outbuf);
	uint64_t byteswritten = inode_data_write(fd, &fs, &in, (uint8_t *)outbuf, len, 0);
	write_inode(fd, &fs, inode_num, &in);
	EXPECT_EQUAL(len, byteswritten);

	char inbuf[len + 1];
	uint64_t bytesread = inode_data_read(fd, &fs, &in, (uint8_t *)inbuf, len, 0);
	EXPECT_EQUAL(len, bytesread);
	inbuf[len] = '\0';
	EXPECT_S(strcmp(inbuf, outbuf) == 0, "Wrong inode content. Expected %s, got %s", outbuf, inbuf);
}

static void test_get_path(void)
{
	write_blank_fs(fd, &fs);
	struct inode_t root_inode;
	read_inode(fd, &fs, 0, &root_inode);

	for (int i = 1; i <= 10; ++i) {
		struct inode_t inode;
		initialize_inode(&inode);
		uint32_t inode_num;
		create_inode(fd, &fs, &inode, &inode_num);
		EXPECT_EQUAL(inode_num, i);
	}
	for (int i = 1; i <= 10; ++i) {
		char name[64];
		strcpy(name, "file-");
		sprintf(name + 5, "%d", i);
		add_inode_to_dir(fd, &fs, 0, &root_inode, i, name);
	}
	for (int i = 1; i <= 10; ++i) {
		char path[64];
		strcpy(path, "/file-");
		sprintf(path + 6, "%d", i);
		uint32_t inode_num;
		struct inode_t inode;
		EXPECT_S(get_path_inode(fd, &fs, path, &inode_num, &inode), "Failed to get inode for path %s", path);
		EXPECT_S(inode_num == i, "Wrong inode number for %s, expected %d, actual %d", path, i, inode_num);
		inode_data_write(fd, &fs, &inode, (const uint8_t *)path, strlen(path), 0);
		write_inode(fd, &fs, inode_num, &inode);
	}
}

int main(int argc, char **argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
		printf("Usage: %s test_file_name\n", argv[0]);
		return argc != 2;
	}

	path = argv[1];

	printf("Creating filesystem\n");
	create_fs(1024*1024);

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Failed to open device");
		exit(1);
	}

	read_fsinfo(fd, &fs);

	print_fs_info();

	printf("Test inode states\n");
	test_inode_state();

	printf("Test inode creation\n");
	test_inode_create();

	printf("Test inode read/write\n");
	test_inode_read_write();

	printf("Test get_path_inode()\n");
	test_get_path();

	close(fd);

	return 0;
}
