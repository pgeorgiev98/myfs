#define FUSE_USE_VERSION 31

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include "myfs.h"
#include "util.h"
#include "helpers.h"
#include "inode_map.h"
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
#include <fcntl.h>

static FILE *log = NULL;

static struct fsinfo_t fs;
static int fd = -1;

static struct inode_map_t inode_map;
static uint32_t file_key_counter = 1;

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

	inode_map_initialize(&inode_map);

	return NULL;
}

static void myfs_destroy(void *private_data)
{
	inode_map_destroy(&inode_map);
}

static int myfs_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	memset(stbuf, 0, sizeof(struct stat));

	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;

	if (!strcmp(path, "/")) {
		inode_num = 0;
		read_inode(fd, &fs, 0, inode);
	} else if (fi && fi->fh) {
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	} else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL)) {
		return -ENOENT;
	}

	uint16_t bs = fs.main_block.block_size;
	uint32_t bcnt = CEIL_DIV(inode->size, bs);
	const struct indirect_block_count_t indirect_bcnt =
		calc_indirect_block_count(bs, bcnt);
	uint64_t total_blocks = indirect_bcnt.total_indirect + bcnt;
	total_blocks *= bs / 512;

	struct timespec mtime = { .tv_sec = inode->mtime, .tv_nsec = 0 };
	struct timespec ctime = { .tv_sec = inode->ctime, .tv_nsec = 0 };

	mode_t mode = (inode->mode & mode_mask);
	if ((inode->mode & mode_ftype_mask) == mode_ftype_dir)
		mode |= S_IFDIR;
	else //if (inode->mode & mode_ftype_mask == mode_ftype_file)
		mode |= S_IFREG;
	stbuf->st_ino = inode_num;
	stbuf->st_mode = mode;
	stbuf->st_nlink = inode->nlinks;
	stbuf->st_uid = inode->uid;
	stbuf->st_gid = inode->gid;
	stbuf->st_size = inode->size;
	stbuf->st_blksize = bs;
	stbuf->st_blocks = total_blocks;
	stbuf->st_atim = mtime;
	stbuf->st_mtim = mtime;
	stbuf->st_ctim = ctime;

	return 0;
}

static int myfs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;

	if (!strcmp(path, "/")) {
		inode_num = 0;
		read_inode(fd, &fs, inode_num, &in);
	} else if (fi && fi->fh) {
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	} else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL)) {
		return -ENOENT;
	}

	inode->mode &= ~0777;
	inode->mode |= mode & 0777;
	write_inode(fd, &fs, inode_num, inode);

	return 0;
}

static int myfs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;

	if (!strcmp(path, "/")) {
		inode_num = 0;
		read_inode(fd, &fs, inode_num, &in);
	} else if (fi && fi->fh) {
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	} else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL)) {
		return -ENOENT;
	}

	inode->uid = uid;
	inode->gid = gid;
	write_inode(fd, &fs, inode_num, inode);

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
	if (!get_path_inode(fd, &fs, path, &cur_inode_num, &cur_inode, NULL, NULL, NULL))
		return -ENOENT;

	uint8_t buffer[fs.main_block.block_size];
	uint64_t s = inode_data_read(fd, &fs, &cur_inode, buffer, sizeof(buffer), 0);
	if (s == 0)
		return 0;

	uint32_t inodes_count;
	uint16_t starting_pos;
	util_read_u32(buffer, &inodes_count);
	util_read_u16(buffer + 0x4, &starting_pos);

	uint64_t file_pos = 0;
	uint64_t pos = starting_pos + 0x6;
	for (uint32_t i = 0; i < inodes_count; ++i) {
		uint16_t name_len;
		uint16_t entry_len;

		EXPECT(pos < s); // TODO: Error handling

		// Load next page if we're at the end of the buffer
		if (pos + 8 > s) {
			file_pos += pos;
			pos = 0;
			s = inode_data_read(fd, &fs, &cur_inode, buffer, sizeof(buffer), file_pos);
		}

		// Read entry header
		util_read_u16(buffer + pos + 0x4, &entry_len);
		util_read_u16(buffer + pos + 0x6, &name_len);
		EXPECT(name_len <= MAX_FILE_NAME_LENGTH); // TODO: error handling

		// Load next page if we're at the end of the buffer
		if (pos + 8 + name_len > s) {
			file_pos += pos;
			pos = 0;
			s = inode_data_read(fd, &fs, &cur_inode, buffer, sizeof(buffer), file_pos);
		}
		EXPECT(pos + 8 + name_len <= s);

		char name[MAX_FILE_NAME_LENGTH + 1];
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
	if (fi && !fi->fh) {
		if (!get_path_inode(fd, &fs, path, &inode_num, &inode, NULL, NULL, NULL))
			return -ENOENT;
		fi->fh = file_key_counter;
		inode_map_insert(&inode_map, file_key_counter, inode_num, &inode);
		++file_key_counter;
	}

	return 0;
}

static int myfs_release(const char *path, struct fuse_file_info *fi)
{
	if (fi && fi->fh) {
		inode_map_remove(&inode_map, fi->fh);
		return 0;
	}
	return 0; // TODO: What error to return?
}

static int myfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;
	if (fi && fi->fh)
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL))
		return -ENOENT;

	return inode_data_read(fd, &fs, inode, (uint8_t *)buf, size, offset);
}

static int myfs_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;
	if (fi && fi->fh)
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL))
		return -ENOENT;

	uint64_t bytes_written = inode_data_write(fd, &fs, inode, (uint8_t *)buf, size, offset);
	write_inode(fd, &fs, inode_num, inode);
	write_main_block(fd, &fs);
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
		if (!get_path_inode(fd, &fs, parent_path, &parent_inode_num, &parent_inode, NULL, NULL, NULL))
			return -ENOENT;
		if ((parent_inode.mode & mode_ftype_mask) != mode_ftype_dir)
			return -ENOTDIR;
	} else {
		parent_inode_num = 0;
		read_inode(fd, &fs, 0, &parent_inode);
	}

	struct inode_t inode;
	uint32_t inode_num;
	initialize_inode(&inode, context->uid, context->gid, (mode & 0777) | mode_ftype_file);
	create_inode(fd, &fs, &inode, &inode_num);
	add_inode_to_dir(fd, &fs, parent_inode_num, &parent_inode, inode_num, &inode, filename);
	write_main_block(fd, &fs);
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
		if (!get_path_inode(fd, &fs, parent_path, &parent_inode_num, &parent_inode, NULL, NULL, NULL))
			return -ENOENT;
		if ((parent_inode.mode & mode_ftype_mask) != mode_ftype_dir)
			return -ENOTDIR;
	} else {
		parent_inode_num = 0;
		read_inode(fd, &fs, 0, &parent_inode);
	}

	struct inode_t inode;
	uint32_t inode_num;
	initialize_inode(&inode, context->uid, context->gid, (mode & 0777) | mode_ftype_dir);
	create_inode(fd, &fs, &inode, &inode_num);
	add_inode_to_dir(fd, &fs, parent_inode_num, &parent_inode, inode_num, &inode, filename);
	write_main_block(fd, &fs);

	return 0;
}

static int myfs_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;
	if (fi && fi->fh)
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL))
		return -ENOENT;

	if ((inode->mode & mode_ftype_mask) == mode_ftype_dir)
		return -EISDIR;

	resize_file(fd, &fs, inode, size);
	write_inode(fd, &fs, inode_num, inode);
	write_main_block(fd, &fs);

	return 0;
}

static int myfs_unlink(const char *path)
{
	uint32_t inode_num, dir_inode_num;
	struct inode_t inode, dir_inode;
	if (!get_path_inode(fd, &fs, path, &inode_num, &inode, &dir_inode_num, &dir_inode, NULL))
		return -ENOENT;

	if ((inode.mode & mode_ftype_mask) == mode_ftype_dir)
		return -EISDIR;

	remove_inode_from_dir(fd, &fs, &dir_inode, inode_num, &inode);
	write_inode(fd, &fs, dir_inode_num, &dir_inode);
	write_main_block(fd, &fs);

	return 0;
}

static int myfs_rmdir(const char *path)
{
	uint32_t inode_num, dir_inode_num;
	struct inode_t inode, dir_inode;
	if (!get_path_inode(fd, &fs, path, &inode_num, &inode, &dir_inode_num, &dir_inode, NULL))
		return -ENOENT;

	if ((inode.mode & mode_ftype_mask) == mode_ftype_file)
		return -ENOTDIR;

	remove_inode_from_dir(fd, &fs, &dir_inode, inode_num, &inode);
	write_inode(fd, &fs, dir_inode_num, &dir_inode);
	write_main_block(fd, &fs);

	return 0;
}

static int myfs_rename(const char *src, const char *dest, unsigned int flags)
{
	if (flags == RENAME_EXCHANGE) {
		uint32_t src_inode_num, dest_inode_num;
		struct inode_t src_inode, dest_inode;
		uint64_t src_offset, dest_offset;
		if (!get_path_inode(fd, &fs, src, &src_inode_num, &src_inode, NULL, NULL, &src_offset) ||
				!get_path_inode(fd, &fs, dest, &dest_inode_num, &dest_inode, NULL, NULL, &dest_offset))
			return -ENOENT;
		uint8_t buf[4];
		util_write_u32(buf, dest_inode_num);
		inode_data_write(fd, &fs, &src_inode, buf, 4, src_offset);
		util_write_u32(buf, src_inode_num);
		inode_data_write(fd, &fs, &dest_inode, buf, 4, dest_offset);

	} else {
		uint32_t src_inode_num, src_dir_inode_num, dest_inode_num, dest_dir_inode_num;
		struct inode_t src_inode, src_dir_inode, dest_inode, dest_dir_inode;

		int src_len = strlen(src), dest_len = strlen(dest);
		char src_dir[src_len + 1], dest_dir[src_len + 1];
		char src_basename[513], dest_basename[513];
		util_split_path(src, src_len, src_dir, src_basename);
		util_split_path(dest, dest_len, dest_dir, dest_basename);

		if (!get_path_inode(fd, &fs, src, &src_inode_num, &src_inode, &src_dir_inode_num, &src_dir_inode, NULL) ||
				!get_path_inode(fd, &fs, dest_dir, &dest_dir_inode_num, &dest_dir_inode, NULL, NULL, NULL))
			return -ENOENT;

		int dest_exists = get_path_inode(fd, &fs, dest, &dest_inode_num, &dest_inode, NULL, NULL, NULL);
		if (flags == RENAME_NOREPLACE && dest_exists)
			return -EEXIST;

		if (flags != RENAME_NOREPLACE && dest_exists) {
			EXPECT(remove_inode_from_dir(fd, &fs, &dest_dir_inode, dest_inode_num, &dest_inode));
			write_inode(fd, &fs, dest_dir_inode_num, &dest_dir_inode);
		}

		add_inode_to_dir(fd, &fs, dest_dir_inode_num, &dest_dir_inode, src_inode_num, &src_inode, dest_basename);
		write_inode(fd, &fs, dest_dir_inode_num, &dest_dir_inode);
		if (!remove_inode_from_dir(fd, &fs, &src_dir_inode, src_inode_num, &src_inode))
			return -ENOENT;
		write_inode(fd, &fs, src_dir_inode_num, &src_dir_inode);
	}
	write_main_block(fd, &fs);

	return 0;
}

static int myfs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi)
{
	uint32_t inode_num;
	struct inode_t in;
	struct inode_t *inode = &in;
	if (fi && fi->fh)
		inode_map_get(&inode_map, fi->fh, &inode_num, &inode);
	else if (!get_path_inode(fd, &fs, path, &inode_num, &in, NULL, NULL, NULL))
		return -ENOENT;

	if (tv[1].tv_nsec == UTIME_NOW) {
		inode->mtime = time(NULL);
		write_inode(fd, &fs, inode_num, inode);
	} else if (tv[1].tv_nsec != UTIME_OMIT) {
		inode->mtime = tv[1].tv_sec;
		write_inode(fd, &fs, inode_num, inode);
	}

	return 0;
}

static const struct fuse_operations myfs_oper = {
	.init       = myfs_init,
	.destroy    = myfs_destroy,
	.getattr    = myfs_getattr,
	.chmod      = myfs_chmod,
	.chown      = myfs_chown,
	.readdir    = myfs_readdir,
	.open       = myfs_open,
	.release    = myfs_release,
	.read       = myfs_read,
	.write      = myfs_write,
	.mknod      = myfs_mknod,
	.mkdir      = myfs_mkdir,
	.truncate   = myfs_truncate,
	.unlink     = myfs_unlink,
	.rmdir      = myfs_rmdir,
	.rename     = myfs_rename,
	.utimens    = myfs_utimens,
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
