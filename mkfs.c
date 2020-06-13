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

	int fd = open(argv[1], O_WRONLY);
	if (fd == -1) {
		perror("Failed to open device:");
		return 1;
	}

	struct fsinfo_t fs;
	write_blank_fs(fd, &fs);

	return 0;
}
