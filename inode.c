#include "myfs.h"
#include "cryp.h"
#include <linux/buffer_head.h>
#include <linux/blk_types.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/stat.h>

///@file

static const struct inode_operations myfs_inode_ops;
static const struct inode_operations myfs_symlink_inode_ops;
static void free_all_blocks(struct inode *);

static inline void inode_no_to_loc(unsigned long ino, u64 *blk_no, int *inode_no_inblock)
{
	*blk_no = MYFS_INODE_STORE_START + ino / MYFS_INODE_PERBLOCK;
	*inode_no_inblock = ino % MYFS_INODE_PERBLOCK;
}

/**
 * myfs_new_inode - allocate and initialize a new inode
 *
 * @param dir directory inode to use as parent
 * @param mode file mode (e.g. S_IFREG, S_IFDIR, S_IFLNK)
 *
 * Allocates a new inode number and initializes a new inode according
 * to the specified mode. If the mode is not a directory, regular file,
 * or symlink, returns an error.
 *
 * @return pointer to the new inode on success, error pointer on failure
 */
static struct inode *myfs_new_inode(struct inode *dir, umode_t mode)
{
	struct inode *inode = NULL;
	struct myfs_incore_inode *incore;
	struct super_block *sb = dir->i_sb;
	ino_t ino;

	/* Return an error if the mode is not a directory, regular file, or symlink */
	if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISLNK(mode))
		return ERR_PTR(-ENOTSUPP);

	/* Allocate a new inode number */
	ino = myfs_ialloc(sb);
	if (ino == 0)
		return ERR_PTR(-ENOSPC);

	/* Get a pointer to the new inode */
	inode = myfs_iget(sb, ino);
	if (IS_ERR(inode))
		return inode;

	/* Initialize the inode's fields based on the specified mode */
	incore = MYFS_I(inode);
	inode->i_size = inode->i_blocks = 0;
	inode->i_op = &myfs_inode_ops;
	incore->flags = 0;
	set_nlink(inode, 1);
	memset(incore->data, 0, sizeof(incore->data));

	if (S_ISDIR(mode))
	{
		/* If the mode is a directory, set the directory file operations */
		inode->i_fop = &myfs_dir_fops;
	}
	else if (S_ISREG(mode))
	{
		/* If the mode is a regular file, set the file operations and file address space operations */
		inode->i_fop = &myfs_file_ops;
		inode->i_mapping->a_ops = &myfs_file_asops;
	}
	else if (S_ISLNK(mode))
	{
		/* If the mode is a symlink, set the symlink inode operations */
		inode->i_op = &myfs_symlink_inode_ops;
		inode->i_link = (char *)incore->data;
	}

	/* Set the inode's owner and timestamps based on the specified mode */
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	inode_init_owner(&init_user_ns, inode, dir, mode);

	return inode;
}

/**
 * rw_disk_inode() - performs read/write of disk inodes
 *
 * @param sb: the super block of the filesystem
 * @param inode_no: the inode number of the inode to be read/written
 * @param target: the pointer to the myfs_disk_inode structure to be written to/read from
 * @param write: flag to indicate whether to write to disk or read from disk
 *
 * Performs read or write operations on the disk inode corresponding to the given inode number.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int rw_disk_inode(struct super_block *sb, unsigned long inode_no, struct myfs_disk_inode *target, bool write)
{
	u64 blk_no = 0;
	int inode_no_inblock = 0;
	struct buffer_head *bh;
	if (inode_no >= MYFS_SB(sb)->inode_count)
		return -EINVAL;
	if (inode_no < MYFS_ROOT_INODE_NO)
		return -EACCES;
	inode_no_to_loc(inode_no, &blk_no, &inode_no_inblock);
	bh = sb_bread(sb, blk_no);
	if (!bh)
		return -EIO;
	struct myfs_disk_inode *disk_copy = ((struct myfs_disk_inode *)bh->b_data) + inode_no_inblock;
	if (write)
	{
		*disk_copy = *target;
		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	}
	else
	{
		*target = *disk_copy;
	}
	brelse(bh);
	return 0;
}

/**
 * myfs_write_inode - writes the incore inode back to disk
 *
 * @param inode: the inode to write to disk
 * @param wbc: the writeback control for the operation
 *
 * @return 0 on success, or a negative error code on failure.
 */
int myfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct myfs_incore_inode *incore = MYFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct myfs_incore_superblock *isb = MYFS_SB(sb);

	/*
	 * Create a disk inode with fields set according to the incore inode.
	 */
	struct myfs_disk_inode disk_inode = {
		.atime = inode->i_atime.tv_sec,
		.block_count = inode->i_blocks,
		.ctime = inode->i_ctime.tv_sec,
		.gid = inode->i_gid.val,
		.link_count = inode->i_nlink,
		.mode = inode->i_mode,
		.mtime = inode->i_mtime.tv_sec,
		.security_info = {.protections = 0},
		.size = i_size_read(inode),
		.uid = inode->i_uid.val,
	};
	int i;
	if (S_ISLNK(inode->i_mode))
		memcpy(disk_inode.data, incore->data, sizeof(disk_inode.data));
	else
		for (i = 0; i < MYFS_NUM_POINTERS; i++)
			disk_inode.data[i] = incore->data[i];
	disk_inode.security_info.protections |= incore->flags & (MYFS_PASS | MYFS_CHNK | MYFS_TRNS);
	if (TEST_OP(incore->flags, MYFS_PASS))
		disk_inode.security_info.hash = incore->hash;

	/* Write the disk inode to the disk. */
	return rw_disk_inode(sb, inode->i_ino, &disk_inode, 1);
}

/**
 * myfs_evict_inode - called at last iput if nlinks is 0
 *
 * @param inode: pointer to the inode struct
 *
 * This function is called when the last reference to an inode is removed.
 * It performs the final cleanup of the inode and frees any resources that it was using.
 */
void myfs_evict_inode(struct inode *inode)
{
	/* Check if nlinks is 0. If not, return without doing anything */
	if (inode->i_nlink != 0)
		return;

	/* Finalize and free any pages associated with the inode's data */
	truncate_inode_pages_final(&inode->i_data);

	/* Free all the blocks associated with the inode */
	free_all_blocks(inode);

	/* Clear the fields of the inode struct that may hinder future use */
	inode->i_mode = 0;
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->i_gid.val = inode->i_uid.val = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = (struct timespec64){
		.tv_sec = 0,
		.tv_nsec = 0,
	};

	/* Clear the flags of the incore inode that may hinder future use */
	struct myfs_incore_inode *incore = MYFS_I(inode);
	incore->flags &= MYFS_REGACC;

	int i;
	/* Clear the data pointers of the incore inode */
	for (i = 0; i < MYFS_NUM_POINTERS; i++)
		incore->data[i] = 0;

	/* Free the inode on the disk */
	myfs_ifree(inode->i_sb, inode->i_ino);
}

/**
 * Implementation of the iget function. This function is responsible for loading the in-memory inode for a given
 * inode number from the disk. If the inode is already in the inode cache, the function just returns it. Otherwise,
 * the function reads the disk inode and initializes the in-memory inode with appropriate values.
 *
 * @param sb: super block pointer
 * @param inode_no: inode number of the inode to be fetched from the disk
 *
 * @return:
 *  - On success, returns the inode pointer.
 *  - On error, returns ERR_PTR with appropriate error code.
 */
struct inode *myfs_iget(struct super_block *sb, unsigned long inode_no)
{
	int ret = 0, i;
	struct myfs_incore_superblock *isb = MYFS_SB(sb);
	struct inode *inode;
	struct myfs_disk_inode disk_inode;
	struct myfs_incore_inode *incore;
	if (inode_no >= isb->inode_count || inode_no < MYFS_ROOT_INODE_NO) /* invalid inode number */
		return ERR_PTR(-EINVAL);
	inode = iget_locked(sb, inode_no); /* get inode from cache */
	if (!inode)
		return ERR_PTR(-ENOMEM);
	incore = MYFS_I(inode);
	if (!TEST_OP(inode->i_state, I_NEW)) /* inode already in cache */
		return inode;
	ret = rw_disk_inode(sb, inode_no, &disk_inode, 0);
	if (ret)
		goto failed;
	/*
	 //todo set vfs inode / incore inode object fields (v.i. : set appropriate operations table)
	 */
	inode->i_ino = inode_no;
	inode->i_sb = sb;
	inode->i_op = &myfs_inode_ops;
	inode->i_mode = le32_to_cpu(disk_inode.mode);
	i_uid_write(inode, le16_to_cpu(disk_inode.uid));
	i_gid_write(inode, le16_to_cpu(disk_inode.gid));
	i_size_write(inode, le32_to_cpu(disk_inode.size));
	inode->i_ctime.tv_sec = (time64_t)(le32_to_cpu(disk_inode.ctime));
	inode->i_ctime.tv_nsec = 0;
	inode->i_atime.tv_sec = (time64_t)(le32_to_cpu(disk_inode.atime));
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_sec = (time64_t)(le32_to_cpu(disk_inode.mtime));
	inode->i_mtime.tv_nsec = 0;
	set_nlink(inode, le32_to_cpu(disk_inode.link_count));
	inode->i_blocks = le32_to_cpu(disk_inode.block_count);
	inode->i_size = le32_to_cpu(disk_inode.size);
	if (S_ISDIR(inode->i_mode))
	{
		inode->i_fop = &myfs_dir_fops;
		for (i = 0; i < MYFS_NUM_POINTERS; i++)
			incore->data[i] = disk_inode.data[i];
	}
	else if (S_ISREG(inode->i_mode))
	{
		for (i = 0; i < MYFS_NUM_POINTERS; i++)
			incore->data[i] = disk_inode.data[i];
		inode->i_fop = &myfs_file_ops;
		inode->i_mapping->a_ops = &myfs_file_asops;
		if (TEST_OP(disk_inode.security_info.protections, MYFS_PASS))
			incore->flags &= ~MYFS_REGACC;
		else
		{
			incore->flags |= MYFS_REGACC;
			incore->key = isb->security_info.key;
		}
		incore->flags |= disk_inode.security_info.protections & (MYFS_CHNK | MYFS_TRNS);
		/*
		 ! for now, all regular files have transposition & encryption. This can be easily made into an option
		 */
	}
	else if (S_ISLNK(inode->i_mode))
	{
		memcpy(MYFS_I(inode)->data, disk_inode.data, inode->i_size);
		inode->i_link = (char *)MYFS_I(inode)->data;
		inode->i_op = &myfs_symlink_inode_ops;
	}

	unlock_new_inode(inode); /* unlock the locked inode if the inode is new */
	return inode;

failed:
	iget_failed(inode);
	return ERR_PTR(ret);
}

/**
 * Find the directory entry matching the given name within the specified directory inode.
 * This function returns a pointer to the matching directory entry, as well as the logical block number,
 * buffer_head and entry number of the entry.
 *
 * @param dir: pointer to the inode representing the directory to search
 * @param name: the name of the entry to find
 * @param res_bh: pointer to the buffer_head pointer that will be updated to point to the buffer_head containing the matching entry
 * @param l_block_no: pointer to the integer that will be updated with the logical block number of the matching entry
 * @param entry_no: pointer to the integer that will be updated with the entry number of the matching entry
 *
 * @return pointer to the matching directory entry if found, NULL otherwise
 */
static struct myfs_dir_entry *dir_find_entry(struct inode *dir, struct qstr name, struct buffer_head **res_bh, sector_t *l_block_no, int *entry_no)
{
	int n_blocks = dir->i_blocks;
	int n_entries = i_size_read(dir) / MYFS_DIR_ENTRY_SIZE;
	int n_seen = 0, n_blocks_seen = 0;
	struct super_block *sb = dir->i_sb;
	sector_t *block_no = MYFS_I(dir)->data;
	struct buffer_head *bh;
	while (n_blocks--)
	{
		bh = sb_bread(sb, (sector_t)*block_no);
		if (!bh)
			return NULL;
		int c = n_entries - n_seen >= MYFS_DIR_ENTRY_PERBLOCK ? MYFS_DIR_ENTRY_PERBLOCK : n_entries - n_seen;
		struct myfs_dir_entry *dentry = (struct myfs_dir_entry *)bh->b_data;
		while (c--)
		{
			if (strncmp(dentry->name, name.name, name.len) == 0)
			{
				*res_bh = bh;
				*l_block_no = n_blocks_seen;
				*entry_no = n_seen;
				return dentry;
			}
			n_seen++;
		}
		brelse(bh);
		n_blocks_seen++;
		block_no++;
	}
	return NULL;
}

/**
 * dir_ino_by_name - find inode number of a given directory entry by name
 *
 * @param dir: pointer to the directory inode
 * @param name: directory entry name to lookup
 * @param ino: pointer to store the found inode number
 *
 * This function looks up the directory entry with the given name in the
 * directory represented by dir inode. If found, the inode number of the
 * corresponding file/directory is stored in ino.
 *
 * @return On success, returns 0 and stores the inode number in ino. On failure, returns an appropriate error code.
 */
static int dir_ino_by_name(struct inode *dir, struct qstr name, ino_t *ino)
{
	// Check if the name length is too long for the file system
	if (name.len > MYFS_NAME_LEN)
		return -ENAMETOOLONG;

	// Search for the directory entry in the directory inode
	struct buffer_head *bh;
	sector_t logical_block_no;
	int entry_no;
	struct myfs_dir_entry *dentry = dir_find_entry(dir, name, &bh, &logical_block_no, &entry_no);

	// If the directory entry is not found, return error
	if (!dentry)
	{
		brelse(bh);
		return -ENOENT;
	}

	// Store the inode number of the found directory entry in @param ino
	*ino = dentry->inode_no;

	// Release the buffer_head
	brelse(bh);

	// Return success
	return 0;
}

/**
 * This function removes a password from a qstr (a string that
 * represents a filename). If the qstr contains a password, it
 * is extracted and used to generate a key and password hash. The
 * password is then removed from the qstr, and the function returns 1.
 * If the qstr does not contain a password, the function returns 0.
 *
 * ! qstr's len will be modified to exclude the password but qstr's hash will not be recalculated. This is intentional.
 *
 *
 * @param qstr: A pointer to the qstr structure representing the filename.
 * @param key: A pointer to a myfs_key struct, where the generated key will be stored.
 * @param hash: A pointer to a myfs_pass_hash struct, where the generated password hash will be stored.
 *
 * @return 1 if the qstr contained a password, 0 otherwise.
 */
static int remove_password_from_qstr(struct qstr *qstr, struct myfs_key *key, struct myfs_pass_hash *hash)
{
	// Find the position of the password separator in the qstr.
	char *pass = memchr(qstr->name, MYFS_PASS_SEP, qstr->len);
	// If there is no password, return 0.
	if (!pass)
		return 0;

	// Generate the key and password hash from the password.
	pass++;
	int passlen = qstr->len - ((void *)pass - (void *)qstr->name);
	passwd_to_key(pass, passlen, key);
	passwd_to_hash(pass, passlen, hash);

	// Remove the password from the qstr.
	qstr->len -= passlen + 1; // 1 extra for

	// Return 1 to indicate that a password was found and removed.
	return 1;
}

/**
 * Lookup directory for entry matching dentry given.
 *
 * @param dir pointer to the parent inode
 * @param dentry pointer to the dentry object being searched for
 * @param flags lookup flags
 *
 * This function searches the directory specified by dir for a directory entry
 * with the name specified in dentry. The entry's inode is returned if it exists.
 *
 * If a password is present in the filename, the password is removed from the
 * dentry name and stored in a key and hash. If the file is password-protected,
 * the password is authenticated and the key is combined with the volume key to
 * obtain the file key.
 *
 * @return a pointer to the resulting dentry object if successful, or an error
 * pointer otherwise.
 */
static struct dentry *myfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{

	/*
	 * there are 3 cases for regular files:
		+ normal path is provided for no-password file : file can be accessed through such dentry
		+ normal path is provided for password file : inode is aliased but file cannot be accessed from such directory
		+ passworded path is provided for password file : file can be accessed
	 */

	struct myfs_incore_inode *incore_dir = MYFS_I(dir);
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct myfs_incore_inode *incore = NULL;
	if (!S_ISDIR(dir->i_mode))
		return ERR_PTR(-ENOTDIR);
	struct myfs_key pass_key;
	struct myfs_pass_hash pass_hash;
	int pass_present = remove_password_from_qstr(&dentry->d_name, &pass_key, &pass_hash);
	if (dentry->d_name.len > MYFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
	struct dentry *result = NULL;
	ino_t ino;
	int res = dir_ino_by_name(dir, dentry->d_name, &ino);
	if (res)
		return ERR_PTR(res);
	inode = myfs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	incore = MYFS_I(inode);
	if (!S_ISREG(inode->i_mode) && pass_present)
	{
		result = ERR_PTR(-ENOTSUPP);
		goto iput;
	}
	if (pass_present)
	{
		if (!TEST_OP(incore->flags, MYFS_PASS))
		{
			result = ERR_PTR(-EINVAL);
			goto iput;
		}
		/*
		 * authenticate
		 */
		if (memcmp(incore->hash.hash, pass_hash.hash, MYFS_HASH_LEN) != 0)
		{
			/*
			 * password hash doesn't match
			 */
			result = ERR_PTR(-EACCES);
			goto iput;
		}
		if (!TEST_OP(incore->flags, MYFS_REGACC))
		{
			/*
			 * if the option is not set, then the key hasn't been calculated
			 * in such case, we combine the password key and the volume key to get the file key
			 */
			combine_keys(&MYFS_SB(sb)->security_info.key, &pass_key);
			incore->key = pass_key;
			incore->flags |= MYFS_REGACC;
			/*
			 ! not marking inode dirty as this flag and password key don't affect the disk inode
			 */
		}
	}
	result = d_splice_alias(inode, dentry);
	if (IS_ERR(result))
		goto iput;
	dentry = result;
	if (S_ISREG(inode->i_mode) && (pass_present || !TEST_OP(incore->flags, MYFS_PASS)))
	{
		ulong f = dentry_get_myfsflags(dentry);
		f |= MYFS_REGACC;
		dentry_set_myfsflags(dentry, f);
	}
	else
	{
		ulong f = dentry_get_myfsflags(dentry);
		f &= ~MYFS_REGACC;
		dentry_set_myfsflags(dentry, f);
	}
	return result;
iput:
	iput(inode);
	return result;
}

/**
 * free_index - frees all the blocks associated with an index block
 *
 * @param sb pointer to the super_block struct
 * @param b_no block number of the index block to be freed
 * @param level level of the index block (direct, indirect, double indirect, or triple indirect)
 *
 * This function frees all the blocks associated with an index block.
 * If the level is less than or equal to 0, or if the block number is 0, it returns without doing anything.
 * If the level is 1, it frees all the blocks pointed to by the index block entries, and clears the entries.
 * If the level is greater than 1, it decrements the level, recursively calls itself for each non-zero entry in the index block,
 * frees all the blocks associated with the non-zero entry, and clears the entry.
 *
 * @return void
 */
int free_index(struct super_block *sb, sector_t b_no, int level)
{
	int c = 0, i;
	if (level <= 0 || b_no == 0) // these cases should generally not occur
		return 0;
	struct buffer_head *bh = sb_bread(sb, b_no);
	if (!bh)
		return 0;
	__le32 *entry = (__le32 *)bh->b_data;
	if (level == 1)
		for (i = 0; i < MYFS_POINTERS_PERBLOCK; i++)
		{
			sector_t bno = *entry;
			if (bno)
			{
				myfs_bfree(sb, bno);
				c++;
			}
			*entry = 0;
		}
	else
	{
		level--;
		for (i = 0; i < MYFS_POINTERS_PERBLOCK; i++)
		{
			sector_t bno = *entry;
			if (bno)
			{
				c += free_index(sb, bno, level);
				myfs_bfree(sb, bno);
			}
			*entry = 0;
		}
	}
	mark_buffer_dirty(bh);
	brelse(bh);
	return c;
}

/**
 * free all blocks (index and data) associated with an inode
 * @param inode pointer to inode whose blocks are to be freed
 *
 * @return void
 */
static void free_all_blocks(struct inode *inode)
{
	sector_t *data = MYFS_I(inode)->data;
	struct super_block *sb = inode->i_sb;
	int i;
	if (S_ISDIR(inode->i_mode))
	{
		for (i = 0; i < MYFS_NUM_POINTERS; i++)
			myfs_bfree(sb, data[i]);
		i_size_write(inode, 0);
		inode->i_blocks = 0;
	}
	else if (S_ISREG(inode->i_mode))
	{
		for (i = 0; i < MYFS_DIR; i++)
			myfs_bfree(sb, *data++);
		for (i = 0; i < MYFS_SINGLE_INDIR; i++)
		{
			free_index(sb, *data, 1);
			myfs_bfree(sb, *data++);
		}
		for (i = 0; i < MYFS_DOUBLE_INDIR; i++)
		{
			free_index(sb, *data, 2);
			myfs_bfree(sb, *data++);
		}
		inode->i_size = 0;
		inode->i_blocks = 0;
	}
}

/**
 * Add an entry to a directory.
 *
 * @param dir the directory inode to add the entry to
 * @param dentry the directory entry to add
 *
 * @return 0 on success, -ENOSPC if there is no space left in the directory, or -EIO if there is an I/O error
 */
static int myfs_add_entry_to_dir(struct inode *dir, struct myfs_dir_entry *dentry)
{
	loff_t size = i_size_read(dir);
	if (size == MYFS_MAX_DIR_SIZE)
		return -ENOSPC;

	int n_entries = size / MYFS_DIR_ENTRY_SIZE;
	sector_t block_no;
	int off = 0;

	if (n_entries % MYFS_DIR_ENTRY_PERBLOCK != 0)
	{
		/* No need for new block allocation */
		block_no = MYFS_I(dir)->data[dir->i_blocks - 1];
		off = n_entries % MYFS_DIR_ENTRY_PERBLOCK;
	}
	else
	{
		block_no = myfs_balloc_and_scrub(dir->i_sb);
		if (block_no == 0)
			return -ENOSPC;
		off = 0;
		MYFS_I(dir)->data[dir->i_blocks++] = block_no;
		mark_inode_dirty(dir);
	}

	struct buffer_head *bh = sb_bread(dir->i_sb, block_no);
	if (!bh)
		return -EIO;

	struct myfs_dir_entry *disk_dentry = (struct myfs_dir_entry *)bh->b_data;
	disk_dentry += off;
	disk_dentry[0] = dentry[0];
	mark_buffer_dirty(bh);
	brelse(bh);
	i_size_write(dir, size + MYFS_DIR_ENTRY_SIZE);
	mark_inode_dirty(dir);
	return 0;
}

/**
 * only remove dentry from dir. doesn't decrease inode link count
 *
 * @param dir: pointer to the parent directory inode
 * @param dentry: pointer to the dentry to be removed
 *
 * @return 0 on success, -ENOENT if the dentry or inode does not exist, -EIO on I/O error
 */
static int myfs_remove_entry_from_dir(struct inode *dir, struct dentry *dentry)
{
	loff_t size = i_size_read(dir);
	struct buffer_head *bh;
	sector_t logical_block_no;
	int entry_no;

	// Find the disk dentry and the buffer head containing it
	struct myfs_dir_entry *disk_dentry = dir_find_entry(dir, dentry->d_name, &bh, &logical_block_no, &entry_no);
	if (IS_ERR(disk_dentry))
		return -ENOENT;

	// Check if the found dentry corresponds to the given inode
	if (d_inode(dentry)->i_ino != disk_dentry->inode_no)
		return -ENOENT;

	// Get the total number of entries and blocks in the directory
	int n_entries = size / MYFS_DIR_ENTRY_SIZE;
	int n_blocks = dir->i_blocks;

	if (n_entries - 1 == entry_no)
	{
		/*
		 * Best case: dentry to be removed is last dentry.
		 * Set the inode number to 0 and clear the name of the dentry.
		 * If the last block contains only 1 entry, free the block.
		 */
		disk_dentry->inode_no = 0;
		memset(disk_dentry->name, 0, MYFS_NAME_LEN);

		if (entry_no % MYFS_DIR_ENTRY_PERBLOCK == 0)
		{
			sector_t block_no = (sector_t)MYFS_I(dir)->data[logical_block_no];
			MYFS_I(dir)->data[logical_block_no] = 0;
			myfs_bfree(dir->i_sb, block_no);
			dir->i_blocks--;
			mark_inode_dirty(dir);
		}
	}
	else if (n_blocks - 1 == logical_block_no)
	{
		/*
		 * Our entry is not the last one but is in the last block.
		 * Copy the last entry to our dentry's location and clear the last entry.
		 */
		int last_entry_offset_inblock = (n_entries - 1) % MYFS_DIR_ENTRY_PERBLOCK;
		disk_dentry[0] = disk_dentry[last_entry_offset_inblock];
		disk_dentry[last_entry_offset_inblock].inode_no = 0;
		memset(disk_dentry[last_entry_offset_inblock].name, 0, MYFS_NAME_LEN);
	}
	else
	{
		/*
		 * Read the last block, copy last entry to our dentry's location and delete the last entry.
		 * If the last block contains only 1 entry, free the block.
		 */
		struct buffer_head *bh2 = sb_bread(dir->i_sb, MYFS_I(dir)->data[n_blocks - 1]);
		if (!bh2)
		{
			brelse(bh);
			return -EIO;
		}
		int last_entry_offset_inblock = (n_entries - 1) % MYFS_DIR_ENTRY_PERBLOCK;
		struct myfs_dir_entry *last_dentry = (struct myfs_dir_entry *)bh2->b_data;
		last_dentry += last_entry_offset_inblock;
		disk_dentry[0] = last_dentry[0];
		last_dentry[0].inode_no = 0;
		memset(last_dentry[0].name, 0, MYFS_NAME_LEN);
		mark_buffer_dirty(bh2);
		brelse(bh2);
		if (last_entry_offset_inblock == 0)
		{
			/*
			 * last block contains only 1 dentry. free the block
			 */
			sector_t block_no = MYFS_I(dir)->data[n_blocks - 1];
			MYFS_I(dir)->data[n_blocks - 1] = 0;
			myfs_bfree(dir->i_sb, block_no);
			dir->i_blocks--;
		}
	}
	i_size_write(dir, size - MYFS_DIR_ENTRY_SIZE);
	mark_inode_dirty(dir);
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

/**
 * Unlinks a dentry. Treats regular files and directories similarly, but should not be used for directories.
 *
 * ! for now, password is not checked when deleting. This can be changed easily by checking the REGACC flag of the dentry
 *
 * Even if nlink drops to 0, blocks are not freed. blocks will be freed by evict_inode on last iput.
 *
 * @param dir The inode of the directory containing the dentry to be unlinked
 * @param dentry The dentry to be unlinked
 * @return 0 on success, or a negative error code on failure
 */
static int myfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = d_inode(dentry);
	struct buffer_head *bh = NULL, *bh2 = NULL;
	unsigned long inode_no = inode->i_ino;
	int err = 0;

	// Remove the entry from the directory
	err = myfs_remove_entry_from_dir(dir, dentry);
	if (err)
		return err;

	// Update the directory's modification and access times
	dir->i_mtime = dir->i_atime = current_time(dir);
	mark_inode_dirty(dir);

	// Decrease the link count of the inode and mark it dirty
	inode_dec_link_count(inode);
	mark_inode_dirty(inode);

	return err;
}

/**
 * myfs_create - create a new file
 *
 * @param ns: user namespace
 * @param dir: inode of the parent directory
 * @param dentry: dentry to create
 * @param mode: file mode to create
 * @param excl: if true, fail if the file already exists
 *
 * @return: 0 on success, negative error code on failure
 */
static int myfs_create(struct user_namespace *ns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	/*
	 * If excl is true, then an error should be returned if dir already has a file with the same name.
	 * We are ignoring excl. We return an error if dentry already exists regardless of excl.
	 */
	struct super_block *sb = dir->i_sb;
	int err;

	/* check filename validity & if it contains a password, handle it */

	struct myfs_key key;
	struct myfs_pass_hash hash;
	int pass_present = remove_password_from_qstr(&dentry->d_name, &key, &hash);

	if (!S_ISREG(mode) && pass_present)
		return -ENOTSUPP;

	if (dentry->d_name.len > MYFS_NAME_LEN)
		return -ENAMETOOLONG;

	/* check if filename already exists and if so, handle appropriately wrt excl -- ignore excl */
	{
		struct buffer_head *bh;
		int entry_no;
		sector_t lblock_no;
		struct myfs_dir_entry *disk_dentry = dir_find_entry(dir, dentry->d_name, &bh, &lblock_no, &entry_no);

		if (!IS_ERR(disk_dentry))
		{
			brelse(bh);
			return -EEXIST;
		}
	}

	if (i_size_read(dir) >= MYFS_MAX_DIR_SIZE)
		return -ENOSPC;

	// returns inode with link count 1
	struct inode *inode = myfs_new_inode(dir, mode);

	if (IS_ERR(inode))
		return PTR_ERR(inode);

	/*
	 * set fields of the new inode
	 */

	struct myfs_incore_inode *incore = MYFS_I(inode);

	if (S_ISREG(mode))
	{
		ulong f = dentry_get_myfsflags(dentry);
		f |= MYFS_REGACC;
		dentry_set_myfsflags(dentry, f);
		incore->flags |= MYFS_REGACC | MYFS_CHNK;
		incore->key = MYFS_SB(sb)->security_info.key;
	}

	if (pass_present)
	{
		incore->flags |= MYFS_PASS;
		incore->hash = hash;
		combine_keys(&incore->key, &key);
		incore->key = key;
	}

	/*
	 * add new directory entry in dir
	 */

	struct myfs_dir_entry disk_dentry = {.inode_no = inode->i_ino, .name = {0}};
	memcpy(disk_dentry.name, dentry->d_name.name, dentry->d_name.len);
	err = myfs_add_entry_to_dir(dir, &disk_dentry);
	if (err)
	{
		discard_new_inode(inode);
		return err;
	}

	d_instantiate(dentry, inode);

	return 0;
}

/**
 * Creates a new directory using myfs_create() function and adds two initial entries: "." and "..".
 *
 * @param ns: User namespace
 * @param dir: Parent directory inode
 * @param dentry: New directory dentry
 * @param mode: Mode of the new directory
 *
 * @return 0 on success, -errno on failure
 */
static int myfs_mkdir(struct user_namespace *ns, struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int err = myfs_create(ns, dir, dentry, mode | S_IFDIR, 1); // Create a new directory using myfs_create() function
	if (err)
		return err;
	struct inode *inode = d_inode(dentry);
	inode_inc_link_count(inode); // Increment nlink count of the newly created directory inode

	/* Add "." and ".." entries to the newly created directory */
	struct myfs_dir_entry disk_dentry = {.inode_no = inode->i_ino, .name = {0}};
	disk_dentry.name[0] = '.';
	inode_inc_link_count(inode);					  // Increment nlink count of the newly created directory inode
	err = myfs_add_entry_to_dir(inode, &disk_dentry); // Add "." entry
	if (err)
	{
		inode_dec_link_count(inode);
		return err;
	}

	disk_dentry.name[1] = '.';
	disk_dentry.inode_no = dir->i_ino;
	inode_inc_link_count(dir);						  // Increment nlink count of the parent directory inode
	err = myfs_add_entry_to_dir(inode, &disk_dentry); // Add ".." entry
	if (err)
	{
		inode_dec_link_count(dir);
		return err;
	}
	return err;
}

/**
 * Creates a hard link between two inodes. Hard links to directories are not allowed.
 *
 * @param old The old dentry for the inode to be linked.
 * @param dir The parent directory's inode for the new link.
 * @param new The new dentry for the link.
 * @return 0 on success; -EMLINK if the directory has already reached its maximum size;
 *         -ENOTSUPP if a password is required but not provided, or if a password is provided but not allowed;
 *         -EINVAL if a password is provided but does not match the linked inode's password;
 *         an error code from myfs_add_entry_to_dir() on failure to add the new link to the directory.
 */
static int myfs_link(struct dentry *old, struct inode *dir, struct dentry *new)
{
	// Get the inode of the old dentry
	struct inode *inode = d_inode(old);

	// Check if the directory size has exceeded the maximum limit
	if (i_size_read(dir) >= MYFS_MAX_DIR_SIZE)
		return -EMLINK;

	// Extract key and hash from the new dentry's name
	struct myfs_key key;
	struct myfs_pass_hash hash;
	int pass_present = remove_password_from_qstr(&new->d_name, &key, &hash);

	// Get the in-core inode
	struct myfs_incore_inode *incore = MYFS_I(inode);

	/*
	 * Check the alignment of security.
	 * Even if password to a passworded file is not provided, a link can be made.
	 * Problem occurs only when a different password is provided.
	 */
	if (!TEST_OP(incore->flags, MYFS_PASS) && pass_present)
		return -ENOTSUPP;

	// Check if the password hash matches that of the old inode
	if (pass_present && memcmp(hash.hash, incore->hash.hash, sizeof(struct myfs_pass_hash)) != 0)
		return -EINVAL;

	// Increment link count of old inode
	inode_inc_link_count(inode);

	// Create disk dentry for new dentry
	struct myfs_dir_entry disk_dentry = {.inode_no = inode->i_ino, .name = {0}};
	memcpy(disk_dentry.name, new->d_name.name, new->d_name.len);

	// Add the new dentry to the directory
	int ret = 0;
	if (ret = myfs_add_entry_to_dir(dir, &disk_dentry))
	{
		// If adding the new dentry to the directory failed, decrement the link count of the old inode
		inode_dec_link_count(inode);
		return ret;
	}

	// Instantiate the new dentry with the old inode
	d_instantiate(new, inode);

	return ret;
}

/**
 * for deletion of directories.
 *
 * @param dir The directory inode that contains the directory to delete.
 * @param dentry The dentry of the directory to delete.
 *
 * @return 0 on success, -ENOTEMPTY if the directory is not empty, or an error code.
 */
static int myfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int err = -ENOTEMPTY;
	if (i_size_read(dir) <= 2 * MYFS_DIR_ENTRY_SIZE) //* checking if directory is empty or not
	{
		err = myfs_unlink(dir, dentry);
		if (!err)
		{
			inode_dec_link_count(inode); //* for deletion of . entry
			inode_dec_link_count(dir);	 //* for deletion of .. entry
		}
	}
	return err;
}

/**
 * for moving files/directories. even if password is present, don't provide it here
 *
 * @param ns user namespace
 * @param old_dir old directory
 * @param old_dentry dentry to be moved
 * @param new_dir new dir to move to
 * @param new_dentry new dentry of the moved file/dir
 *
 * @returns 0 on success, negative error number otherwise
 */
static int myfs_rename(struct user_namespace *ns, struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry, unsigned int flags)
{
	if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -ENOTSUPP;
	struct myfs_key key;
	struct myfs_pass_hash hash;

	int pass_present = remove_password_from_qstr(&new_dentry->d_name, &key, &hash);
	if (pass_present) //* change of password is not supported
		return -ENOTSUPP;
	if (new_dentry->d_name.len > MYFS_NAME_LEN)
		return -ENAMETOOLONG;
	if (i_size_read(new_dir) >= MYFS_MAX_DIR_SIZE) //* new directory has no space
		return -ENOSPC;
	{
		sector_t lblk;
		int eno;
		struct buffer_head *bh;
		struct myfs_dir_entry *disk_dentry = dir_find_entry(new_dir, new_dentry->d_name, &bh, &lblk, &eno);
		if (!IS_ERR(disk_dentry))
		{ /*
		   * there is already an entry in new dir with same name
		   */
			brelse(bh);
			return -EALREADY;
		}
	}
	struct myfs_dir_entry disk_dentry = {.inode_no = 0, .name = {0}};
	disk_dentry.inode_no = d_inode(old_dentry)->i_ino;
	memcpy(disk_dentry.name, old_dentry->d_name.name, old_dentry->d_name.len);
	int err = myfs_add_entry_to_dir(new_dir, &disk_dentry);
	if (err)
		return err;
	d_instantiate(new_dentry, d_inode(old_dentry));
	err = myfs_remove_entry_from_dir(old_dir, old_dentry);
	if (err)
	{
		myfs_remove_entry_from_dir(new_dir, new_dentry);
		d_drop(new_dentry);
		return err;
	}
	if (S_ISDIR(d_inode(new_dentry)->i_mode))
	{
		/*
		 * if a directory is being moved, the .. entry should be updated
		 ! assumption .. entry will be 2nd entry and will be found in 1st block
		 */
		struct inode *inode = d_inode(new_dentry);
		struct buffer_head *bh = sb_bread(inode->i_sb, MYFS_I(inode)->data[0]);
		if (!bh)
			return -EIO;
		struct myfs_dir_entry *dotdot = ((struct myfs_dir_entry *)bh->b_data) + 1;
		dotdot->inode_no = new_dir->i_ino;
		mark_buffer_dirty(bh);
		brelse(bh);
	}
	d_drop(old_dentry);
	return err;
}

/**
 * Creates a symbolic link with given symlink name in the given directory.
 *
 * @param ns - user namespace
 * @param dir - pointer to inode of the directory in which symbolic link needs to be created
 * @param dentry - pointer to dentry of the symbolic link
 * @param symname - name of the symbolic link
 *
 * @return - returns 0 on success and appropriate error code on failure
 **/
static int myfs_symlink(struct user_namespace *ns, struct inode *dir, struct dentry *dentry, const char *symname)
{
	// Check symlink length
	int l = strlen(symname);
	if (l > MYFS_MAX_SYMLINK_LEN)
		return -ENAMETOOLONG;

	// Check password presence
	struct myfs_key key;
	struct myfs_pass_hash hash;
	int pass_present = remove_password_from_qstr(&dentry->d_name, &key, &hash);
	if (pass_present)
		return -ENOTSUPP;

	// Check dentry name length
	if (dentry->d_name.len > MYFS_NAME_LEN)
		return -ENAMETOOLONG;

	// Check directory size
	if (i_size_read(dir) >= MYFS_MAX_DIR_SIZE)
		return -ENOSPC;

	// Create inode for symlink
	struct inode *inode = myfs_new_inode(dir, S_IFLNK | S_IRWXUGO); //* all rwx permissions set for symlink
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	// Add symlink to directory
	struct myfs_dir_entry disk_dentry = {.inode_no = inode->i_ino, .name = {0}};
	int err = myfs_add_entry_to_dir(dir, &disk_dentry);
	if (err)
	{
		/*
		 * entry couldn't be added. free acquired inode
		 */
		myfs_ifree(inode->i_sb, inode->i_ino);
		inode->i_mode = 0;
		set_nlink(inode, 0);
		mark_inode_dirty(inode);
		iput(inode);
		return err;
	}

	// Copy symlink name to inode data
	memcpy(inode->i_link, symname, l);
	i_size_write(inode, l);
	mark_inode_dirty(inode);

	// Instantiate dentry with inode
	d_instantiate(dentry, inode);

	return err;
}

static const struct inode_operations myfs_inode_ops = {
	.lookup = myfs_lookup, /* lookup a name in directory  */
	.create = myfs_create, /* create a file  */
	.link = myfs_link,	   /* add a link (hard) to an inode */
	.unlink = myfs_unlink, /* remove a link (rm) */
	.mkdir = myfs_mkdir,
	.rmdir = myfs_rmdir,
	.rename = myfs_rename,	 /* move */
	.symlink = myfs_symlink, /* create symbolic link */
};

/**
 * Get the symlink value of an inode
 *
 * @param dentry the dentry struct of the inode to retrieve the symlink value from
 * @param inode the inode struct to retrieve the symlink value from
 * @param done a delayed_call struct
 * @return the symlink value of the inode
 */
static const char *myfs_getsymlink(struct dentry *dentry, struct inode *inode, struct delayed_call *done)
{
	return inode->i_link;
}

static const struct inode_operations myfs_symlink_inode_ops = {
	.get_link = myfs_getsymlink, /* gives link string */
};