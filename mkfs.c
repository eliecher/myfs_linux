#include "myfs_disk_structs.h"
#include "cryp.c"
#define __KERNEL__
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <linux/stat.h>
#define NUM_INODES 4000

static int wr_sb(int fd, const struct myfs_disk_superblock *sb)
{
	lseek(fd, 0, SEEK_SET);
	return (write(fd, sb, sizeof(*sb)) != sizeof(*sb));
}

static int wr_inodes(int fd, const struct myfs_disk_superblock *sb)
{
	/* we won't worry about other inodes. we will just write the root inode */
	uint32_t rbno = MYFS_ROOT_INODE_NO / MYFS_INODE_PERBLOCK;
	uint32_t roff = MYFS_ROOT_INODE_NO % MYFS_INODE_PERBLOCK;
	time_t sec;
	sec = time(NULL);
	struct myfs_disk_inode di = {
		.atime = sec,
		.block_count = 1,
		.ctime = sec,
		.gid = 0,
		.link_count = 2,
		.mode = (S_IFDIR | S_IRWXU | S_IRGRP | S_IROTH | S_IXGRP | S_IXOTH), // setting dir flag and permissions to 0755
		.mtime = sec,
		.security_info = {.protections = 0}, // directories have no protections
		.size = 2 * MYFS_DIR_ENTRY_SIZE,	 // . and .. directories
		.uid = 0,
	};
	memset(di.data, 0, sizeof(di.data));
	di.data[0] = sb->data_block_start;
	lseek(fd, (MYFS_INODE_STORE_START + rbno) * MYFS_BLOCK_SIZE + roff, SEEK_SET);
	return (write(fd, &di, sizeof(di)) != sizeof(di));
}

static int wr_ibmap(int fd, const struct myfs_disk_superblock *sb)
{
	lseek(fd, sb->inode_bitmap_start_block * MYFS_BLOCK_SIZE, SEEK_SET);
	unsigned char blk[MYFS_BLOCK_SIZE];
	memset(blk, 0, MYFS_BLOCK_SIZE);
	int i;
	for (i = 0; i < sb->inode_bitmap_num_blocks; i++)
	{
		if (write(fd, blk, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE)
			return -1;
	}
	int used = MYFS_ROOT_INODE_NO + 1;
	int used_bytes = used / 8;
	int used_bits = used % 8;
	lseek(fd, sb->inode_bitmap_start_block * MYFS_BLOCK_SIZE, SEEK_SET);
	if (used_bytes > MYFS_BLOCK_SIZE)
	{
		memset(blk, 0xff, MYFS_BLOCK_SIZE);
		while (used_bytes > MYFS_BLOCK_SIZE)
		{
			if (write(fd, blk, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE)
				return -1;
			used_bytes -= MYFS_BLOCK_SIZE;
		}
	}
	if (used_bytes || used_bits)
	{
		memset(blk, 0, MYFS_BLOCK_SIZE);
		memset(blk, 0xff, used_bytes);
		if (used_bits)
		{
			unsigned char byte = 0xff & ~((1 << (8 - used_bits)) - 1);
			blk[used_bytes] = byte;
		}
		if (write(fd, blk, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE)
			return -1;
	}
	return 0;
}

static int wr_bbmap(int fd, const struct myfs_disk_superblock *sb)
{
	lseek(fd, sb->data_bitmap_start_block * MYFS_BLOCK_SIZE, SEEK_SET);
	unsigned char blk[MYFS_BLOCK_SIZE];
	memset(blk, 0, MYFS_BLOCK_SIZE);
	int i;
	for (i = 0; i < sb->data_bitmap_num_blocks; i++)
	{
		if (write(fd, blk, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE)
			return -1;
	}
	lseek(fd, sb->data_bitmap_start_block * MYFS_BLOCK_SIZE, SEEK_SET);
	unsigned char byte = 0x80; // 1 data block consumed for root directory
	return (write(fd, &byte, 1) != 1);
}

static int wr_dblks(int fd, const struct myfs_disk_superblock *sb)
{
	lseek(fd, sb->data_block_start * MYFS_BLOCK_SIZE, SEEK_SET);
	unsigned char blk[MYFS_BLOCK_SIZE];
	memset(blk, 0, MYFS_BLOCK_SIZE);
	struct myfs_dir_entry dentry;
	memset(dentry.name, 0, MYFS_NAME_LEN);
	dentry.inode_no = MYFS_ROOT_INODE_NO;
	dentry.name[0] = '.';
	memcpy(blk, &dentry, MYFS_DIR_ENTRY_SIZE);
	dentry.name[1] = '.';
	memcpy(blk + MYFS_DIR_ENTRY_SIZE, &dentry, MYFS_DIR_ENTRY_SIZE);
	return (write(fd, blk, MYFS_BLOCK_SIZE) != MYFS_BLOCK_SIZE);
}

int main(int argc, char **argv)
{
	char mssg[100];
	if (argc < 3)
	{
		sprintf(mssg, "use:%s <device-name> <password>\n", argv[0]);
		perror(mssg);
		return -1;
	}
	char dev_name[100];
	char pass[MYFS_PASS_MAX_LEN + 1];
	strcpy(dev_name, argv[1]);
	strcpy(pass, argv[2]);
	struct myfs_pass_hash hash;
	passwd_to_hash(pass, strlen(pass), &hash);
	int fd = open(dev_name, O_RDWR);
	if (fd < 0)
	{
		sprintf(mssg, "failed to open device\n");
		perror(mssg);
		return -1;
	}
	int err = 0;
	struct stat stat;
	err = fstat(fd, &stat);
	if (err)
	{
		perror("couldn't do fstat");
		goto close;
	}
	if (!S_ISBLK(stat.st_mode))
	{
		perror("not a block device\n");
		goto close;
	}
	unsigned long sz_in_bytes = 0;
	err = ioctl(fd, BLKGETSIZE64, &sz_in_bytes);
	if (err)
	{
		perror("couldn't get size\n");
		goto close;
	}
	printf("BLKGETSIZE64=%ul\n", sz_in_bytes);
	unsigned long n_blocks = sz_in_bytes / MYFS_BLOCK_SIZE;
	unsigned long n_inodes = NUM_INODES;
	unsigned long n_inode_blocks = n_inodes / MYFS_INODE_PERBLOCK + (n_inodes % MYFS_INODE_PERBLOCK != 0);
	unsigned long ibmap_start = MYFS_INODE_STORE_START + n_inode_blocks;
	unsigned long ibmap_len = (n_inodes / MYFS_BLOCK_SIZE_IN_BITS) + (n_inodes % MYFS_BLOCK_SIZE_IN_BITS != 0);
	unsigned long bbmap_start = ibmap_start + ibmap_len;
	unsigned long n_blk_data_sec = n_blocks - bbmap_start;
	printf("data section blocks count=%ul\n", n_blk_data_sec);
	printf("n_inode_blocks=%ul\n", n_inode_blocks);
	if (n_blk_data_sec < 2)
	{
		perror("very small device\n");
		goto close;
	}
	uint32_t n_data_blks = ((unsigned long long)n_blk_data_sec * MYFS_BLOCK_SIZE_IN_BITS) / (MYFS_BLOCK_SIZE_IN_BITS + 1);
	uint32_t bbmap_len = n_blk_data_sec - n_data_blks;
	while (bbmap_len * MYFS_BLOCK_SIZE_IN_BITS < n_data_blks)
	{
		n_data_blks--;
		bbmap_len++;
	};
	uint32_t data_blk_start = bbmap_start + bbmap_len;
	struct myfs_disk_superblock dsb = {
		.block_count = n_blocks,
		.data_bitmap_num_blocks = bbmap_len,
		.data_bitmap_start_block = bbmap_start,
		.data_block_count = n_data_blks,
		.data_block_start = data_blk_start,
		.flags = MYFS_PASS,
		.free_data_block_count = n_data_blks - 1,
		.free_inode_count = (n_inodes - MYFS_ROOT_INODE_NO - 1), // all inodes upto root inode are not usable
		.inode_bitmap_num_blocks = ibmap_len,
		.inode_bitmap_start_block = ibmap_start,
		.inode_count = n_inodes,
		.magic = MYFS_MAGIC,
		.security_info = {.hash = hash},
	};
	printf("n_blocks=%8x\n", n_blocks);
	printf("data bmap len=%8x\n", bbmap_len);
	printf("data bmap start=%8x\n", bbmap_start);
	printf("data blk count=%8x\n", n_data_blks);
	printf("data block start=%8x\n", data_blk_start);
	printf("flags=%8x\n", MYFS_PASS);
	printf("num free data=%8x\n", n_data_blks - 1);
	printf("num free inode=%8x\n", n_inodes - 1 - MYFS_ROOT_INODE_NO);
	printf("ibmap len=%8x\n", ibmap_len);
	printf("ibmap start=%8x\n", ibmap_start);
	printf("n_inodes=%8x\n", n_inodes);
	printf("magic=%4x\n", MYFS_MAGIC);
	printf("hash = ");
	for (int i = 0; i < MYFS_HASH_LEN; i++)
		printf("%2x ", hash.hash[i]);
	printf("\n");
	const struct myfs_disk_superblock *sb = &dsb;
	err = wr_sb(fd, sb);
	if (err)
		goto close;
	err = wr_inodes(fd, sb);
	if (err)
		goto close;
	err = wr_ibmap(fd, sb);
	if (err)
		goto close;
	err = wr_bbmap(fd, sb);
	if (err)
		goto close;
	err = wr_dblks(fd, sb);
	if (err)
		goto close;
	;
close:
	close(fd);
	if (err)
	{
		perror("ERROR\n");
	}
	return err;
}
