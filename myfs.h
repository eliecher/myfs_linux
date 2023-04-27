#ifndef MYFS_H
#define MYFS_H
// #define __KERNEL__
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include "cryp.h"
#include "myfs_disk_structs.h"

#define TEST_OP(FLAG, OPTION) (((FLAG) & (OPTION)) == (OPTION))

#define MYFS_I(vfs_i) container_of(vfs_i, struct myfs_incore_inode, vfs_inode)
#define MYFS_SB(vfs_sb) ((struct myfs_incore_superblock *)(vfs_sb->s_fs_info))

struct myfs_incore_superblock
{

	__u32 block_count;												   /* total blocks */
	__u32 inode_count, free_inode_count;							   /* number of inodes--total,free */
	__u32 ibmap_start_block, ibmap_block_count, ibmap_last_block_bits; /* inode bitmap info */
	__u32 bbmap_start_block, bbmap_block_count, bbmap_last_block_bits; /* block bitmap info */
	__u32 data_block_start, data_block_count, free_data_block_count;   /* data blocks info */
	__u32 flags;
	struct mutex i, b; /* mutex to ensure correctness when using bitmaps */
	struct
	{
		struct myfs_pass_hash hash; /* stores password hash */
		struct myfs_key key;		/* volume key */
	} security_info;
};

struct myfs_incore_inode
{
	__u32 flags;
	sector_t data[MYFS_NUM_POINTERS]; /* index for dir/reg, path for symlink */
	struct myfs_pass_hash hash;		  /* hash of password. used when password is present */
	struct myfs_key key;			  /* file key */
	struct inode vfs_inode;			  /* vfs's inode */
};

/* @var */
extern struct address_space_operations myfs_file_asops;

/* @var */
extern struct file_operations myfs_file_ops;
/* @var */
extern struct file_operations myfs_dir_fops;

extern int __init init_inode_cache(void);
extern void destroy_inode_cache(void);
extern int myfs_fill_super(struct super_block *sb, void *data, int silent);

/*
 * inode functions
 */

extern void myfs_truncate_blocks(struct inode *inode, loff_t offset);
extern struct inode *myfs_iget(struct super_block *sb, unsigned long ino);
extern int myfs_write_inode(struct inode *vfs_inode, struct writeback_control *wbc);
extern void myfs_evict_inode(struct inode *inode);
extern int free_index(struct super_block *sb, sector_t b_no, int level);
/*
 * dentry functions
 */
extern void dentry_set_myfsflags(struct dentry *dentry, unsigned long flags);
extern unsigned long dentry_get_myfsflags(struct dentry *dentry);

/*
 * alloc and free functions
 */

extern sector_t myfs_balloc(struct super_block *);
extern sector_t myfs_balloc_and_scrub(struct super_block *);
extern void myfs_scrub_block(void *block);
extern void myfs_bfree(struct super_block *, sector_t);
extern ino_t myfs_ialloc(struct super_block *);
extern void myfs_ifree(struct super_block *, ino_t);
#endif