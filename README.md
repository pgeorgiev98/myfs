# myfs

This is a university project for a (very) simple disk filesystem written using *FUSE* in C. The used layout is fairly similar to the one used in the *Second Extended Filesystem* (ext2)

# Building

	git clone https://github.com/pgeorgiev98/myfs
	cd myfs
	mkdir build
	meson ..
	ninja

# Testing

The easiest way to test the filesystem would be to use a file and a loop device:

	dd if=/dev/zero of=disk.bin bs=1M count=1024
	./mkfs.myfs disk.bin                                  # Format the filesystem
	./fsinfo disk.bin                                     # Print info about the filesystem
	mkdir mountpoint                                      # Create a mount point
	cd mountpoint
	./myfs --dev=$PWD/disk.bin ./mountpoint/ -o auto_unmount -s -f  # Mount the filesystem
	...                                                   # The filesystem will run on foreground,
	                                                      # so you can access it from another terminal
	fusermount -u .                                       # Unmount the filesystem
