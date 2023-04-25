
#include "myfs.h"
#include "cryp.h"
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/uio.h>

///@file

/**
 * myfs_iterate - read a directory and add entries to ctx
 * @param filep: the file pointer to the directory file
 * @param ctx: the directory context structure
 *
 * This function is called by the VFS layer when a directory is being read.
 * It reads the directory entries from the blocks of the directory inode,
 * and adds them to the directory context structure.
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int myfs_iterate(struct file *filep, struct dir_context *ctx)
{
	// Get the inode and in-core inode structures for the directory file
	struct inode *inode = file_inode(filep);
	struct myfs_incore_inode *incore_inode = MYSF_I(inode);

	// Get the superblock for the filesystem
	struct super_block *sb = inode->i_sb;

	// Check that the file is actually a directory
	if (!S_ISDIR(inode->i_mode))
		return -ENOTDIR;

	// Calculate the number of blocks and entries in the directory
	int n_blocks = inode->i_blocks;
	int n_entries = i_size_read(inode) / MYFS_DIR_ENTRY_SIZE;
	int n_seen = 0; // Number of entries seen so far
	int err = 0;	// Error code, if any

	int i;
	// Iterate over the blocks of the directory inode
	for (i = 0; i < n_blocks; i++)
	{
		// Read the block into a buffer head structure
		struct buffer_head *bh = sb_bread(sb, incore_inode->data[i]);
		if (!bh)
		{
			// If there was an error reading the block, set the error code and continue to the next block
			err = -EIO;
			continue;
		}

		// Determine the number of directory entries in this block and get a pointer to the first one
		int n_inblock = min(n_entries - n_seen, MYFS_DIR_ENTRY_PERBLOCK);
		int to_see = n_inblock;
		struct myfs_dir_entry *dentry = bh->b_data;

		// Iterate over the directory entries in this block
		while (to_see--)
		{
			// If this directory entry is valid (i.e., it has a non-zero inode number), add it to the directory context
			if (dentry->inode_no != 0 && (err = dir_emit(ctx, dentry->name, strnlen(dentry->name, MYFS_NAME_LEN), dentry->inode_no, DT_UNKNOWN)))
				break;
			ctx->pos++; // Increment the directory context position
		}

		n_seen += n_inblock; // Update the number of entries seen
		brelse(bh);			 // Release the buffer head
	}

	return err; // Return the error code, if any
}

const struct file_operations myfs_dir_fops = {
	.owner = THIS_MODULE,
	.iterate_shared = myfs_iterate,
};