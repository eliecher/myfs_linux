#ifndef MYFS_STRUCTS_H
#include <linux/types.h>
#include <linux/fs.h>
#include "cryp.h"
#define MYFS_STRUCTS_H
#define MYFS_MAGIC 0xABCD

#define MYFS_BLOCK_SIZE_BITS 12
#define MYFS_BLOCK_SIZE (1UL << MYFS_BLOCK_SIZE_BITS)
#define MYFS_BLOCK_SIZE_IN_BITS ((1UL << MYFS_BLOCK_SIZE_BITS) << 3)
#define MYFS_SUPERBLOCK 0
#define MYFS_INODE_STORE_START (MYFS_SUPERBLOCK + 1)
#define MYFS_INODE_PERBLOCK (MYFS_BLOCK_SIZE / sizeof(struct myfs_disk_inode))
#define MYFS_ROOT_INODE_NO 2

struct myfs_disk_superblock
{
	__le16 magic; /*magic number. helps in verifying that volumes is formatted with myfs*/
	/* there is some padding here */
	__le32 block_count;				 /* total number of blocks */
	__le32 inode_count;				 /* number of inodes */
	__le32 free_inode_count;		 /* number of inodes free currently */
	__le32 inode_bitmap_start_block; /* block number where free inode bitmap starts */
	__le32 inode_bitmap_num_blocks;	 /* number of blocks used by inode bitmap */
	__le32 data_bitmap_start_block;	 /* block number where free block bitmap starts */
	__le32 data_bitmap_num_blocks;	 /* number of blocks used by block bitmap */
	__le32 data_block_start;		 /* block number of 1st data block */
	__le32 data_block_count;		 /* number of data blocks */
	__le32 free_data_block_count;	 /* number of free data blocks currently */
	__le32 flags;
	struct
	{
		struct myfs_pass_hash hash; /* to authenticate */
	} security_info;
};

#define MYFS_MAX_INDEX_DEPTH 3
#define MYFS_DIR 0
#define MYFS_SINGLE_INDIR 12
#define MYFS_DOUBLE_INDIR 4
#define MYFS_NUM_POINTERS (MYFS_DIR + MYFS_SINGLE_INDIR + MYFS_DOUBLE_INDIR)

#define MYFS_POINTERS_PERBLOCK (MYFS_BLOCK_SIZE / 4)

#define MYFS_DIRECT_DATA_BLOCKS 1
#define MYFS_SINGLE_INDIRECT_DATA_BLOCKS (MYFS_POINTERS_PERBLOCK * MYFS_DIRECT_DATA_BLOCKS)
#define MYFS_DOUBLE_INDIRECT_DATA_BLOCKS (MYFS_POINTERS_PERBLOCK * MYFS_SINGLE_INDIRECT_DATA_BLOCKS)

#define MYFS_DIRECT_CAPACITY (MYFS_DIRECT_DATA_BLOCKS * MYFS_BLOCK_SIZE)
#define MYFS_SINGLE_INDIRECT_CAPACITY (MYFS_SINGLE_INDIRECT_DATA_BLOCKS * MYFS_BLOCK_SIZE)
#define MYFS_DOUBLE_INDIRECT_CAPACITY (MYFS_DOUBLE_INDIRECT_DATA_BLOCKS * MYFS_BLOCK_SIZE)

#define MYFS_MAX_FILE_BLOCKS (MYFS_DIR * MYFS_DIRECT_CAPACITY + MYFS_SINGLE_INDIR * MYFS_SINGLE_INDIRECT_DATA_BLOCKS + MYFS_DOUBLE_INDIR * MYFS_DOUBLE_INDIRECT_DATA_BLOCKS)

#define MYFS_MAX_FILE_SIZE (MYFS_MAX_FILE_BLOCKS * MYFS_BLOCK_SIZE)
#define MYFS_MAX_DIR_SIZE (MYFS_NUM_POINTERS * MYFS_BLOCK_SIZE)
#define MYFS_MAX_SYMLINK_LEN (MYFS_NUM_POINTERS * 8)

struct myfs_disk_inode
{
	__le32 mode;	   /* same as vfs's mode -- permissions, filetype */
	__le16 uid, gid;   /* owner user,group */
	__le16 link_count; /* number of links (hard) */
	__le32 ctime, atime, mtime;
	__le32 block_count;				/* number of blocks in use (not metadata blocks) */
	__le32 size;					/* size in bytes */
	__le32 data[MYFS_NUM_POINTERS]; /* area to store pointers -- index */
	struct
	{
		__le16 protections;			/* specifies protections */
		struct myfs_pass_hash hash; /* to authenticate */
	} security_info;
};

#define MYFS_REGACC 0b0001
#define MYFS_PASS 0b0010
#define MYFS_CHNK 0b0100
#define MYFS_TRNS 0b1000

#define MYFS_NAME_LEN 20

struct myfs_dir_entry
{
	__le32 inode_no;
	char name[MYFS_NAME_LEN];
};

#define MYFS_DIR_ENTRY_SIZE (sizeof(struct myfs_dir_entry))

#define MYFS_DIR_ENTRY_PERBLOCK (BLOCK_SIZE / MYFS_DIR_ENTRY_SIZE)

#endif