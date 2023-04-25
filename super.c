#include "myfs.h"
#include "cryp.h"
#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/log2.h>
#include <linux/quotaops.h>
#include <linux/uaccess.h>
#include <linux/dax.h>
#include <linux/iversion.h>
#define CONFIG_TINY_RCU
#include <linux/rcupdate.h>

///@file

/*
 * the inode cache -- stores inodes for myfs type file systems
 */
static struct kmem_cache *myfs_inode_cache;

/*
 * constructor for new inode in cache
 */
static void init_new_cache(void *foo)
{
	struct myfs_incore_inode *cache_line = (struct myfs_incore_inode *)foo;
	cache_line->flags = 0;
	inode_init_once(&cache_line->vfs_inode);
}

/*
 * initialize inode cache
 */
int __init init_inode_cache(void)
{
	myfs_inode_cache = kmem_cache_create("myfs_inode_cache", sizeof(struct myfs_incore_inode), 0, (SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT | SLAB_MEM_SPREAD), init_new_cache);
	if (myfs_inode_cache == NULL)
		return -ENOMEM;
	return 0;
}

/*
 * destroys inode cache at time of module removal -- probably at shutdown
 */
void destroy_inode_cache(void)
{
	/*
	//todo add a barrier so that all in-core inodes are flushed
	 */
	// rcu_barrier_tasks();
	kmem_cache_destroy(myfs_inode_cache);
}

/*
 * allocate new inode in memory (cache)
 */
struct inode *myfs_alloc_inode(struct super_block *sb)
{
	struct myfs_incore_inode *incore = (struct myfs_incore_inode *)kmem_cache_alloc(myfs_inode_cache, GFP_KERNEL);
	if (!incore)
		return NULL;
	inode_init_once(&incore->vfs_inode);
	return &incore->vfs_inode;
}

/*
 * frees incore inode in cache
 */
void myfs_free_incore_inode(struct inode *vfs_inode)
{
	kmem_cache_free(myfs_inode_cache, MYFS_I(vfs_inode));
}

/*
 * sync fs with disk. do it synchronously if wait is set
 */
int myfs_sync_fs(struct super_block *sb, int wait)
{
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	struct myfs_disk_superblock *disk_superblock;
	struct buffer_head *bh = sb_bread(sb, MYFS_SUPERBLOCK);
	if (!bh)
		return -EIO;
	disk_superblock = (struct myfs_disk_superblock *)bh->b_data;
	mutex_lock(&isb->b);
	mutex_lock(&isb->i);
	*disk_superblock = (struct myfs_disk_superblock){
		.block_count = isb->block_count,
		.data_bitmap_num_blocks = isb->bbmap_block_count,
		.data_bitmap_start_block = isb->bbmap_start_block,
		.data_block_count = isb->data_block_count,
		.data_block_start = isb->data_block_start,
		.flags = isb->flags,
		.free_data_block_count = isb->free_data_block_count,
		.free_inode_count = isb->free_inode_count,
		.inode_bitmap_num_blocks = isb->ibmap_block_count,
		.inode_bitmap_start_block = isb->ibmap_start_block,
		.inode_count = isb->inode_count,
		.magic = sb->s_magic,
		.security_info = {.hash = isb->security_info.hash},
	};

	mutex_unlock(&isb->b);
	mutex_unlock(&isb->i);
	mark_buffer_dirty(bh);
	if (wait)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/*
 * gives status of the filesystem
 */
static int myfs_statfs(struct dentry *dentry, struct kstatfs *stat)
{
	struct super_block *sb = dentry->d_sb;
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	mutex_lock(&isb->b);
	mutex_lock(&isb->i);
	stat->f_bavail = isb->free_data_block_count;
	stat->f_bfree = isb->free_data_block_count;
	stat->f_blocks = isb->block_count;
	stat->f_bsize = MYFS_BLOCK_SIZE;
	stat->f_ffree = isb->free_inode_count;
	stat->f_files = isb->inode_count;
	stat->f_frsize = MYFS_BLOCK_SIZE;
	stat->f_namelen = MYFS_NAME_LEN;
	stat->f_flags = isb->flags;
	stat->f_type = MYFS_MAGIC;
	mutex_unlock(&isb->b);
	mutex_unlock(&isb->i);
	return 0;
}

/*
 * super operations
 */
struct super_operations myfs_super_ops = {
	.alloc_inode = myfs_alloc_inode,
	.destroy_inode = myfs_free_incore_inode,
	.free_inode = myfs_free_incore_inode,
	.write_inode = myfs_write_inode,
	.evict_inode = myfs_evict_inode,
	.sync_fs = myfs_sync_fs,
	.statfs = myfs_statfs,
};

int extract_pass(char **data)
{
	if (!data[0][0])
		return -1;
	if (strcasecmp(data, "pass=") == 0)
	{
		*data = *data + 5;
		char *end = memchr(*data, MYFS_PASS_SEP, MYFS_PASS_MAX_LEN + 1);
		if (!end)
			return -EINVAL;
		return end - data;
	}
	return -1;
}

/*
 * fills superblock during mount
 */
int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *bh = NULL;
	struct myfs_incore_superblock *isb = NULL;
	struct myfs_disk_superblock *dsb = NULL;
	struct inode *root_inode = NULL;
	int err = 0;
	sb->s_magic = MYFS_MAGIC;
	sb_set_blocksize(sb, MYFS_BLOCK_SIZE);
	sb->s_maxbytes = MYFS_MAX_FILE_SIZE;
	sb->s_op = &myfs_super_ops;
	int pass_len = extract_pass(&data);
	if (pass_len < 0)
	{
		err = -ENOTSUPP;
		goto out;
	}
	struct myfs_pass_hash hash;
	struct myfs_key key;
	passwd_to_hash(data, pass_len, &hash);
	passwd_to_key(data, pass_len, &hash);
	data += pass_len + 1;
	bh = sb_bread(sb, MYFS_SUPERBLOCK);
	if (!bh)
	{
		err = -EIO;
		goto out;
	}
	dsb = (struct myfs_disk_superblock *)bh->b_data;
	if (le16_to_cpu(dsb->magic) != sb->s_magic)
	{
		err = -EINVAL;
		goto release;
	}
	if (memcmp(hash.hash, dsb->security_info.hash.hash, MYFS_HASH_LEN) != 0)
	{
		err = -EACCES;
		goto release;
	}
	isb = kzalloc(sizeof(struct myfs_incore_superblock), GFP_KERNEL);
	if (!isb)
	{
		err = -ENOMEM;
		goto release;
	}

	*isb = (struct myfs_incore_superblock){
		.bbmap_block_count = dsb->data_bitmap_num_blocks,
		.bbmap_last_block_bits = (dsb->data_block_count - 1) % MYFS_BLOCK_SIZE_IN_BITS,
		.bbmap_start_block = dsb->data_bitmap_start_block,
		.block_count = dsb->block_count,
		.data_block_count = dsb->data_block_count,
		.data_block_start = dsb->data_block_start,
		.flags = dsb->flags,
		.free_data_block_count = dsb->free_data_block_count,
		.free_inode_count = dsb->free_inode_count,
		.ibmap_block_count = dsb->inode_bitmap_num_blocks,
		.ibmap_last_block_bits = (dsb->inode_count - 1) % MYFS_BLOCK_SIZE_IN_BITS,
		.ibmap_start_block = dsb->inode_bitmap_start_block,
		.inode_count = dsb->inode_count,
		.security_info = {.key = key, .hash = hash},
	};
	mutex_init(&isb->b);
	mutex_init(&isb->i);
	brelse(bh);
	root_inode = myfs_iget(sb, MYFS_ROOT_INODE_NO);
	if (IS_ERR(root_inode))
	{
		err = PTR_ERR(root_inode);
		goto out;
	}
	if (!S_ISDIR(root_inode->i_mode))
	{
		err = -ENOTDIR;
		goto iput;
	}
	inode_init_owner(&init_user_ns, root_inode, NULL, root_inode->i_mode);
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
	{
		err = -ENOMEM;
		goto iput;
	}
iput:
	iput(root_inode);
free_isb:
	kfree(isb);
out:
	return err;
release:
	brelse(bh);
	goto out;
}

static int change_one_bit_tell_sucess(u8 block[MYFS_BLOCK_SIZE], int32_t bitpos)
{
	u8 mask = 1 << (7 - (bitpos % 8));
	int byte = bitpos / 8;
	if (block[byte] & mask)
	{
		block[byte] &= ~mask;
		return 0;
	}
	return -1;
}

static int32_t find_and_change_zero_bit(u8 block[MYFS_BLOCK_SIZE], uint32_t max_bitpos)
{
	u8 *start = block, *end = block + max_bitpos / 8 + 1;
	while (start != end)
	{
		if (*start = 0xff)
			start++;
		else
			break;
	}
	if (start == end)
		return -1;
	u8 x = *start;
	int off = 0;
	if (x & 0x80)
		off = 0;
	if (x & 0x40)
		off = 1;
	if (x & 0x20)
		off = 2;
	if (x & 0x10)
		off = 3;
	if (x & 0x08)
		off = 4;
	if (x & 0x04)
		off = 5;
	if (x & 0x02)
		off = 6;
	if (x & 0x01)
		off = 7;
	int32_t ans = (start - block) + off;
	if (ans > max_bitpos)
		return -1;
	*start = x | (((u8)1) << (7 - off));
	return ans;
}

sector_t myfs_balloc(struct super_block *sb)
{
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	sector_t bno = 0, seen = 0;
	mutex_lock(&isb->b);
	if (isb->free_data_block_count == 0)
		goto unlock_and_out;
	struct buffer_head *bh = NULL;
	int f = 0,i;
	sector_t bbmap = isb->bbmap_start_block;
	for ( i = 0; i < isb->bbmap_block_count; i++)
	{
		bh = sb_bread(sb, bbmap);
		uint32_t last_bitpos = (i == isb->bbmap_block_count - 1) ? isb->bbmap_last_block_bits : MYFS_BLOCK_SIZE_IN_BITS - 1;
		if (bh)
		{
			int pos = find_and_change_zero_bit(bh->b_data, last_bitpos);
			if (pos < 0)
				seen += last_bitpos + 1;
			else
			{
				bno = seen + pos;
				f = 1;
				isb->free_data_block_count--;
				mark_buffer_dirty(bh);
			}
			brelse(bh);
		}
		if (f)
		{
			bno += isb->data_block_start;
			goto unlock_and_out;
		}
		bbmap++;
	}
	bno = 0;
unlock_and_out:
	mutex_unlock(&isb->b);
	return bno;
}

void myfs_bfree(struct super_block *sb, sector_t bno)
{
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	mutex_lock(&isb->b);
	if (bno < isb->data_block_start || bno > isb->block_count)
		goto unlock;
	bno -= isb->data_block_start;
	sector_t lblk = bno / MYFS_BLOCK_SIZE_IN_BITS;
	uint32_t off = bno % MYFS_BLOCK_SIZE_IN_BITS;
	if (lblk >= isb->bbmap_block_count)
		goto unlock;
	lblk += isb->bbmap_start_block;
	struct buffer_head *bh = sb_bread(sb, lblk);
	if (bh)
	{
		if (change_one_bit_tell_sucess(bh->b_data, off) == 0)
		{
			mark_buffer_dirty(bh);
			isb->free_data_block_count++;
		}
		brelse(bh);
	}
unlock:
	mutex_unlock(&isb->b);
}

ino_t myfs_ialloc(struct super_block *sb)
{
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	ino_t ino = 0, seen = 0;
	mutex_lock(&isb->i);
	if (isb->free_inode_count == 0)
		goto unlock_and_out;
	struct buffer_head *bh = NULL;
	sector_t ibmap = isb->ibmap_start_block;
	int i;
	for (i = 0; i < isb->ibmap_block_count; i++)
	{
		bh = sb_bread(sb, ibmap);
		uint32_t last_bitpos = (i == isb->ibmap_block_count - 1) ? isb->ibmap_last_block_bits : MYFS_BLOCK_SIZE_IN_BITS - 1;
		if (bh)
		{
			int pos = find_and_change_zero_bit(bh->b_data, last_bitpos);
			if (pos < 0)
				seen += last_bitpos + 1;
			else
			{
				ino = seen + pos;
				isb->free_data_block_count--;
				mark_buffer_dirty(bh);
			}
			brelse(bh);
		}
		if (ino)
			goto unlock_and_out;
		ibmap++;
	}
	ino = 0;
unlock_and_out:
	mutex_unlock(&isb->i);
	return ino;
}

void myfs_ifree(struct super_block *sb, ino_t ino)
{
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	mutex_lock(&isb->i);
	if (ino < MYFS_ROOT_INODE_NO || ino >= isb->inode_count)
		goto unlock;
	sector_t lblk = ino / MYFS_BLOCK_SIZE_IN_BITS;
	uint32_t off = ino % MYFS_BLOCK_SIZE_IN_BITS;
	if (lblk >= isb->ibmap_block_count)
		goto unlock;
	lblk += isb->ibmap_start_block;
	struct buffer_head *bh = sb_bread(sb, lblk);
	if (bh)
	{
		if (change_one_bit_tell_sucess(bh->b_data, off) == 0)
		{
			mark_buffer_dirty(bh);
			isb->free_data_block_count++;
		}
		brelse(bh);
	}
unlock:
	mutex_unlock(&isb->i);
}

sector_t myfs_balloc_and_scrub(struct super_block *sb)
{
	sector_t bno = myfs_balloc(sb);
	if (bno)
	{
		struct buffer_head *bh = sb_bread(sb, bno);
		if (bh)
		{
			myfs_scrub_block(bh->b_data);
			mark_buffer_dirty(bh);
			brelse(bh);
		}
	}
	return bno;
}

inline void myfs_scrub_block(void *block)
{
	memset(block, 0, MYFS_BLOCK_SIZE);
}