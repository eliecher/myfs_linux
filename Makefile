# Makefile for the MYFS filesystem
#

obj-$(CONFIG_MYFS_FS) += myfs.o

myfs-y := cryp.o dir.o file.o fs.o inode.o super.o