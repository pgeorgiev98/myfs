#include "myfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
	if (argc != 2) {
		return 1;
	}

	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Failed to open device:");
		return 1;
	}

	struct fsinfo_t fs;
	read_fsinfo(fd, &fs);

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

	return 0;
}
