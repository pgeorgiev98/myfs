#define FUSE_USE_VERSION 31

#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "util.h"
#include "asserts.h"

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <sys/stat.h>

static FILE *log = NULL;

static struct fsinfo_t fs;
static int fd = -1;

/*
 * Command line options
 *
 * We can't set default values for the char* fields here because
 * fuse_opt_parse would attempt to free() them when the user specifies
 * different values on the command line.
 */
static struct options {
	const char *devpath;
	int show_help;
} options;

#define OPTION(t, p)                           \
    { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--dev=%s", devpath),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static void *myfs_init(struct fuse_conn_info *conn,
		struct fuse_config *cfg)
{
	fd = open(options.devpath, O_RDWR);
	if (fd == -1) {
		perror("Failed to open device");
		exit(1);
	}

	read_fsinfo(fd, &fs);

	return NULL;
}

static int myfs_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (!strcmp(path, "/")) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}

	uint32_t inode_num;
	struct inode_t inode;
	if (get_path_inode(fd, &fs, path, &inode_num, &inode)) {
		mode_t mode = (inode.mode & mode_mask);
		if ((inode.mode & mode_ftype_mask) == mode_ftype_dir)
			mode |= S_IFDIR;
		else //if (inode.mode & mode_ftype_mask == mode_ftype_file)
			mode |= S_IFREG;
		stbuf->st_mode = mode;
		stbuf->st_nlink = 1; // TODO
		stbuf->st_size = inode.size;
		stbuf->st_uid = inode.uid;
		stbuf->st_gid = inode.gid;
		return 0;
	}

	return -ENOENT;
}

static int myfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t inode;

	if (!strcmp(path, "/")) {
		inode_num = 0;
		read_inode(fd, &fs, inode_num, &inode);
	} else if (!get_path_inode(fd, &fs, path, &inode_num, &inode)) {
		return -ENOENT;
	}

	inode.mode &= ~0777;
	inode.mode |= mode & 0777;
	write_inode(fd, &fs, inode_num, &inode);

	return 0;
}

static int myfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t inode;

	if (!strcmp(path, "/")) {
		inode_num = 0;
		read_inode(fd, &fs, inode_num, &inode);
	} else if (!get_path_inode(fd, &fs, path, &inode_num, &inode)) {
		return -ENOENT;
	}

	inode.uid = uid;
	inode.gid = gid;
	write_inode(fd, &fs, inode_num, &inode);

	return 0;
}

static int myfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);

	uint32_t cur_inode_num;
	struct inode_t cur_inode;
	if (!get_path_inode(fd, &fs, path, &cur_inode_num, &cur_inode))
		return -ENOENT;

	uint8_t buffer[fs.main_block.block_size];
	uint64_t s = inode_data_read(fd, &fs, &cur_inode, buffer, sizeof(buffer), 0);
	if (s == 0)
		return 0;

	uint64_t pos = 4;
	uint32_t inodes_count;
	util_read_u32(buffer, &inodes_count);

	for (uint32_t i = 0; i < inodes_count; ++i) {
		uint16_t name_len;
		uint16_t entry_len;
		EXPECT(pos + 8 <= s);
		util_read_u16(buffer + pos + 0x4, &entry_len);
		util_read_u16(buffer + pos + 0x6, &name_len);
		EXPECT(pos + 8 + name_len <= s);
		char name[513]; // TODO
		memcpy(name, buffer + pos + 0x8, name_len);
		name[name_len] = '\0';
		filler(buf, name, NULL, 0, 0);
		pos += entry_len;
	}

	return 0;
}

static int myfs_open(const char *path, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t inode;
	if (!get_path_inode(fd, &fs, path, &inode_num, &inode))
		return -ENOENT;

	return 0;
}

static int myfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t inode;
	if (!get_path_inode(fd, &fs, path, &inode_num, &inode))
		return -ENOENT;

	return inode_data_read(fd, &fs, &inode, (uint8_t *)buf, size, offset);
}

static int myfs_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t inode;
	if (!get_path_inode(fd, &fs, path, &inode_num, &inode))
		return -ENOENT;

	uint64_t bytes_written = inode_data_write(fd, &fs, &inode, (uint8_t *)buf, size, offset);
	write_inode(fd, &fs, inode_num, &inode);
	return bytes_written;
}

static int myfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	struct fuse_context *context = fuse_get_context();
	int len = strlen(path);
	int i;
	for (i = len - 1; i >= 0; --i)
		if (path[i] == '/')
			break;
	char filename[len - i];
	strncpy(filename, path + i + 1, len - i - 1);
	filename[len - i - 1] = '\0';

	uint32_t parent_inode_num;
	struct inode_t parent_inode;
	if (i != 0) {
		char parent_path[i + 1];
		strncpy(parent_path, path, i);
		parent_path[i] = '\0';
		if (!get_path_inode(fd, &fs, parent_path, &parent_inode_num, &parent_inode))
			return -ENOENT;
		if ((parent_inode.mode & mode_ftype_mask) != mode_ftype_dir)
			return -ENOTDIR;
	} else {
		parent_inode_num = 0;
		read_inode(fd, &fs, 0, &parent_inode);
	}

	struct inode_t inode = {
		.ctime = 0,
		.mtime = 0,
		.size = 0,
		.blocks = 0,
		.uid = context->uid,
		.gid = context->gid,
		.mode = (mode & 0777) | mode_ftype_file,
	};
	uint32_t inode_num;
	create_inode(fd, &fs, &inode, &inode_num);
	add_inode_to_dir(fd, &fs, parent_inode_num, &parent_inode, inode_num, filename);
	return 0;
}

static int myfs_mkdir(const char *path, mode_t mode)
{
	struct fuse_context *context = fuse_get_context();
	int len = strlen(path);
	int i;
	for (i = len - 1; i >= 0; --i)
		if (path[i] == '/')
			break;
	char filename[len - i];
	strncpy(filename, path + i + 1, len - i - 1);
	filename[len - i - 1] = '\0';

	uint32_t parent_inode_num;
	struct inode_t parent_inode;
	if (i != 0) {
		char parent_path[i + 1];
		strncpy(parent_path, path, i);
		parent_path[i] = '\0';
		if (!get_path_inode(fd, &fs, parent_path, &parent_inode_num, &parent_inode))
			return -ENOENT;
		if ((parent_inode.mode & mode_ftype_mask) != mode_ftype_dir)
			return -ENOTDIR;
	} else {
		parent_inode_num = 0;
		read_inode(fd, &fs, 0, &parent_inode);
	}

	struct inode_t inode = {
		.ctime = 0,
		.mtime = 0,
		.size = 0,
		.blocks = 0,
		.uid = context->uid,
		.gid = context->gid,
		.mode = (mode & 0777) | mode_ftype_dir,
	};
	uint32_t inode_num;
	create_inode(fd, &fs, &inode, &inode_num);
	add_inode_to_dir(fd, &fs, parent_inode_num, &parent_inode, inode_num, filename);

	return 0;
}

static const struct fuse_operations myfs_oper = {
	.init       = myfs_init,
	.getattr    = myfs_getattr,
	.chmod      = myfs_chmod,
	.chown      = myfs_chown,
	.readdir    = myfs_readdir,
	.open       = myfs_open,
	.read       = myfs_read,
	.write      = myfs_write,
	.mknod      = myfs_mknod,
	.mkdir      = myfs_mkdir,
};

int main(int argc, char *argv[])
{
	log = fopen("log", "w");

	int ret;
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

	/* Set defaults -- we have to use strdup so that
	   fuse_opt_parse can free the defaults if other
	   values are specified */
	options.devpath = NULL;

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	/* When --help is specified, first print our own file-system
	   specific help text, then signal fuse_main to show
	   additional help (by adding `--help` to the options again)
	   without usage: line (by setting argv[0] to the empty
	   string) */
	if (options.show_help) {
		assert(fuse_opt_add_arg(&args, "--help") == 0);
		args.argv[0][0] = '\0';
	} else if (options.devpath == NULL) {
		fprintf(stderr, "Device path expected\n");
		return 1;
	}

	ret = fuse_main(args.argc, args.argv, &myfs_oper, NULL);
	fuse_opt_free_args(&args);
	fclose(log);
	return ret;
}
