#define FUSE_USE_VERSION 31

#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <sys/stat.h>

static FILE *log = NULL;

struct file
{
	char name[256];
};
static struct file files[100];
static int files_count = 0;

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

static void *hello_init(struct fuse_conn_info *conn,
		struct fuse_config *cfg)
{
	printf("Init\n");
	return NULL;
}

static int hello_getattr(const char *path, struct stat *stbuf,
			 struct fuse_file_info *fi)
{
	memset(stbuf, 0, sizeof(struct stat));
	if (!strcmp(path, "/")) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	}

	for (int i = 0; i < files_count; ++i) {
		if (!strcmp(path + 1, files[i].name)) {
			stbuf->st_mode = S_IFREG | 0444;
			stbuf->st_nlink = 1;
			stbuf->st_size = 0;
			return 0;
		}
	}

	return -ENOENT;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi,
			 enum fuse_readdir_flags flags)
{
	if (strcmp(path, "/"))
		return -ENOENT;

	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	for (int i = 0; i < files_count; ++i) {
		filler(buf, files[i].name, NULL, 0, 0);
	}

	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
	for (int i = 0; i < files_count; ++i) {
		if (!strcmp(path + 1, files[i].name)) {
			if ((fi->flags & O_ACCMODE) != O_RDONLY)
				return -EACCES;
			return 0;
		}
	}

	return -ENOENT;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	printf("Read\n");

	int f = -1;
	for (int i = 0; i < files_count; ++i) {
		if (!strcmp(path + 1, files[i].name)) {
			f = i;
			break;
		}
	}
	if (f == -1)
		return -ENOENT;

	size = 0;
	return size;
}

static int hello_mknod(const char *path, mode_t mode, dev_t dev)
{
	for (int i = 0; i < files_count; ++i)
		if (!strcmp(path + 1, files[i].name))
			return -EEXIST;

	int f = files_count++;
	strcpy(files[f].name, path + 1);

	return 0;
}

static const struct fuse_operations hello_oper = {
	.init       = hello_init,
	.getattr    = hello_getattr,
	.readdir    = hello_readdir,
	.open       = hello_open,
	.read       = hello_read,
	.mknod      = hello_mknod,
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
	}

	if (options.devpath == NULL) {
		fprintf(stderr, "Device path expected\n");
		return 1;
	}

	ret = fuse_main(args.argc, args.argv, &hello_oper, NULL);
	fuse_opt_free_args(&args);
	fclose(log);
	return ret;
}
