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
			"Total number of blocks:    %u\n"
			"Number of usable blocks:   %u\n"
			"Number of free blocks:     %u\n"
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

static void test_inode_read_write1(void)
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

static void test_inode_read_write2(uint32_t file_count, uint32_t *file_sizes)
{
	struct file {
		struct inode_t inode;
		uint32_t num;
		uint8_t *data;
	};
	struct file files[file_count];
	for (uint32_t i = 0; i < file_count; ++i) {
		files[i].data = (uint8_t *)malloc(file_sizes[i]);
		initialize_inode(&files[i].inode);
		create_inode(fd, &fs, &files[i].inode, &files[i].num);
	}

	for (uint32_t i = 0; i < file_count; ++i) {
		for (int j = 0; j < file_sizes[i]; ++j)
			files[i].data[j] = 3*j + 5*j + 7;
		uint64_t written = inode_data_write(fd, &fs, &files[i].inode, files[i].data, file_sizes[i], 0);
		EXPECT_EQUAL(written, file_sizes[i]);
		write_inode(fd, &fs, files[i].num, &files[i].inode);
		{
			uint8_t *buf = (uint8_t *)malloc(file_sizes[i]);
			uint64_t readb = inode_data_read(fd, &fs, &files[i].inode, buf, file_sizes[i], 0);
			EXPECT_EQUAL(readb, file_sizes[i]);
			for (uint32_t j = 0; j < file_sizes[i]; ++j) {
				if (files[i].data[j] != buf[j]) {
					EXPECT_S(0, "Wrong file content (file %u; difference at byte %u/%u)", i, j, file_sizes[i]);
					break;
				}
			}
			free(buf);
		}
	}

	for (uint32_t i = 0; i < file_count; ++i) {
		uint8_t *buf = (uint8_t *)malloc(file_sizes[i]);
		uint64_t readb = inode_data_read(fd, &fs, &files[i].inode, buf, file_sizes[i], 0);
		EXPECT_EQUAL(readb, file_sizes[i]);
		for (int j = 0; j < file_sizes[i]; ++j) {
			if (files[i].data[j] != buf[j]) {
				EXPECT_S(0, "Wrong file content (file %u; difference at byte %u/%u)", i, j, file_sizes[i]);
				break;
			}
		}
		free(buf);
	}

	for (uint32_t i = 0; i < file_count; ++i)
		free(files[i].data);
}

static void test_inode_read_write_random(uint32_t fsize)
{
	struct inode_t inode;
	uint32_t inode_num;
	uint8_t *data = (uint8_t *)malloc(fsize);
	for (uint32_t i = 0; i < fsize; ++i)
		data[i] = i;
	initialize_inode(&inode);
	create_inode(fd, &fs, &inode, &inode_num);
	inode_data_write(fd, &fs, &inode, data, fsize, 0);
	for (int i = 0; i < 100; ++i) {
		uint64_t pos = (i * ((fsize / 3) + 7)) % fsize;
		uint8_t buf[1] = {i};
		inode_data_write(fd, &fs, &inode, buf, 1, pos);
		data[pos] = buf[0];
	}
	EXPECT_EQUAL(inode.size, fsize);
	write_inode(fd, &fs, inode_num, &inode);

	uint8_t *actual = (uint8_t *)malloc(fsize);
	inode_data_read(fd, &fs, &inode, actual, fsize, 0);

	for (uint32_t i = 0; i < fsize; ++i) {
		if (data[i] != actual[i]) {
			EXPECT_S(0, "Wrong file content (difference at byte %u/%u)", i, fsize);
			break;
		}
	}

	free(actual);
	free(data);
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
	}
}

int main(int argc, char **argv)
{
	{
		int help = (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")));
		int wrong_arg = (argc != 3 || (strcmp(argv[2], "short") && strcmp(argv[2], "long")));
		if (help || wrong_arg) {
			const char *usage =
				"Usage: %s test_file_name [short|long]\n"
				"\n"
				"  short - Performs a quick test with a few small files\n"
				"  long  - Performs a longer test with a lots of large files\n";
			printf(usage, argv[0]);
			return wrong_arg;
		}
	}
	int short_test = !strcmp(argv[2], "short");

	path = argv[1];

	printf("=== Creating filesystem ===\n");
	create_fs(short_test ? 16*1024*1024 : 2UL*1024*1024*1024);

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Failed to open device");
		exit(1);
	}

	read_fsinfo(fd, &fs);

	print_fs_info();

	printf("=== Test inode states ===\n");
	test_inode_state();

	printf("=== Test inode creation ===\n");
	test_inode_create();

	printf("=== Test basic file read/write ===\n");
	test_inode_read_write1();

	// Test with files up to 4KB
	printf("=== Test small file read/write ===\n");
	uint32_t file_sizes[10] = {1300, 1500, 1000, 2300, 4000, 2500, 2300, 1000, 500, 3000};
	test_inode_read_write2(10, file_sizes);

	if (!short_test) {
		// Test with files up to 4MB
		printf("=== Test medium file read/write ===\n");
		for (int i = 0; i < 10; ++i)
			file_sizes[i] *= 1000;
		test_inode_read_write2(10, file_sizes);

		// Test with files up to 400MB
		printf("=== Test large file read/write ===\n");
		uint32_t large_file_sizes[3] = {400, 300, 500};
		for (int i = 0; i < 3; ++i)
			large_file_sizes[i] *= 1000000;
		test_inode_read_write2(3, large_file_sizes);
	}

	printf("=== Test random file write() ===\n");
	test_inode_read_write_random(short_test ? 50000 : 16*1024*1024);

	printf("=== Test get_path_inode() ===\n");
	test_get_path();

	close(fd);

	return 0;
}
