#include "myfs.h"
#include "cryp.h"
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/uio.h>

///@file

inline void dentry_set_myfsflags(struct dentry *dentry, unsigned long flags)
{
	dentry->d_fsdata = (void *)flags;
}
inline unsigned long dentry_get_myfsflags(struct dentry *dentry)
{
	return (unsigned long)dentry->d_fsdata;
}

/**
 * @param lblkno logical block number
 * @param chain array of logical index entry numbers that must be followed to reach block number lblkno
 *
 * Fills chain which tells logical entry numbers to follow
 *
 * @return -1 on failure otherwise depth of index/ number of index lookups required
 */
int lblk_to_lindex(sector_t lblkno, uint chain[MYFS_MAX_INDEX_DEPTH])
{
	sector_t passed = 0;
	if (lblkno < MYFS_DIRECT_DATA_BLOCKS * MYFS_DIR)
	{
		chain[0] = lblkno;
		return 1;
	}
	passed += MYFS_DIRECT_DATA_BLOCKS * MYFS_DIR;
	if (lblkno < passed + MYFS_SINGLE_INDIRECT_DATA_BLOCKS * MYFS_SINGLE_INDIR)
	{
		chain[0] = MYFS_DIR + (lblkno - passed) / MYFS_SINGLE_INDIRECT_DATA_BLOCKS;
		chain[1] = (lblkno - passed) % MYFS_SINGLE_INDIRECT_DATA_BLOCKS;
		return 2;
	}
	passed += MYFS_SINGLE_INDIRECT_DATA_BLOCKS * MYFS_SINGLE_INDIR;
	if (lblkno < passed + MYFS_DOUBLE_INDIRECT_DATA_BLOCKS * MYFS_DOUBLE_INDIR)
	{
		chain[0] = MYFS_DIR + MYFS_SINGLE_INDIR + (lblkno - passed) / MYFS_DOUBLE_INDIRECT_DATA_BLOCKS;
		chain[1] = ((lblkno - passed) % MYFS_DOUBLE_INDIRECT_DATA_BLOCKS) / MYFS_SINGLE_INDIRECT_DATA_BLOCKS;
		chain[2] = (lblkno - passed) % MYFS_SINGLE_INDIRECT_DATA_BLOCKS;
		return 3;
	}
	return -1;
}

/**
 * @param inode inode of the file
 * @param block pointer to a block number. answer will be stored here. If block has not been allocated, block number of last allocated block in lookup chain is stored here
 * @param chain chain of logical index entry numbers
 * @param dep depth of index or number of lookups
 * @param allocate_to_complete pointer to an integer. stores number of allocations required to complete the chain
 *
 * The purpose of this function is to translate chain into a block number. However, it may be so that no block has been allocated corresponding
 * to the chain. In such case, it calculates number of disk blocks to allocate to complete the chain. It also finds the block number of the
 * last allocated disk block in the chain. If there is no such block (chain broken from the inode itself) block is set to 0;
 *
 * @return 0 on success, negative error code otherwise
 */
int lindex_to_pblock(struct inode *inode, sector_t *block, uint chain[MYFS_MAX_INDEX_DEPTH], int dep, int *allocate_to_complete)
{
	sector_t bno;
	struct buffer_head *bh;
	struct super_block *sb;
	int pentry_no;
	sb = inode->i_sb;
	*allocate_to_complete = 0;
	switch (dep)
	{
	case 1:
		*block = MYFS_I(inode)->data[chain[0]];
		*allocate_to_complete = (*block == 0);
		return 0;
	case 2:
		bno = MYFS_I(inode)->data[chain[0]];
		*block = bno;
		if (bno == 0)
		{ /* index block and data block need to be allocated */
			*allocate_to_complete = 2;
			return 0;
		}
		bh = sb_bread(sb, bno);
		if (!bh)
			return -EIO;
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, bno, chain[1]);
		bno = *((__le32 *)bh->b_data + pentry_no);
		brelse(bh);
		if (bno == 0)
			*allocate_to_complete = 1; /* only data block to be allocated */
		else
			*block = bno;
		return 0;
	case 3:
		bno = MYFS_I(inode)->data[chain[0]];
		*block = bno;
		if (bno == 0)
		{
			*allocate_to_complete = 3;
			return 0;
		}
		bh = sb_bread(sb, bno);
		if (!bh)
			return -EIO;
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, bno, chain[1]);
		bno = *((__le32 *)bh->b_data + pentry_no);
		brelse(bh);
		if (bno == 0)
		{
			*allocate_to_complete = 2;
			return 0;
		}
		else
			*block = bno;
		bh = sb_bread(sb, bno);
		if (!bh)
			return -EIO;
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, bno, chain[2]);
		bno = *((__le32 *)bh->b_data + pentry_no);
		brelse(bh);
		if (bno == 0)
			*allocate_to_complete = 1;
		else
			*block = bno;
		return 0;
	}
	return -EINVAL;
}

/**
 * allocate a chain of blocks (1 data block and num_alloc-1 index blocks) and fill in the index blocks so that data block is properly indexed
 *
 * @param inode     pointer to inode structure
 * @param num_alloc number of blocks to be allocated
 * @param[out] first_alloc sector number of first allocated block
 * @param[out] data_block sector number of data block
 * @param chain array of index block numbers
 *
 * @return 0 on success, -ENOSPC if block allocation fails, -EIO if read/write to block device fails, -1 for invalid num_alloc value
 */
int allocate_blocks_with_index(struct inode *inode, int num_alloc, sector_t *first_alloc, sector_t *data_block, uint *chain)
{
	struct super_block *sb = inode->i_sb;
	if (num_alloc == 1)
	{
		*first_alloc = *data_block = myfs_balloc_and_scrub(sb);
		if (*data_block == 0)
			return -ENOSPC;
		return 0;
	}
	if (num_alloc == 2)
	{
		uint pentry_no;
		__le32 *entry;
		struct buffer_head *bh;
		*first_alloc = myfs_balloc(sb);
		if (*first_alloc == 0)
			return -ENOSPC;
		*data_block = myfs_balloc_and_scrub(sb);
		if (*data_block == 0)
		{
			myfs_bfree(sb, *first_alloc);
			*first_alloc = *data_block = 0;
			return -ENOSPC;
		}
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, *first_alloc, chain[0]);
		bh = sb_bread(inode->i_sb, *first_alloc);
		if (!bh)
		{
			myfs_bfree(sb, *data_block);
			myfs_bfree(sb, *first_alloc);
			*first_alloc = *data_block = 0;
			return -EIO;
		}
		myfs_scrub_block(bh->b_data);
		entry = (__le32 *)bh->b_data;
		entry += pentry_no;
		*entry = *data_block;
		brelse(bh);
		return 0;
	}
	if (num_alloc == 3)
	{
		sector_t middle;
		uint pentry_no;
		__le32 *entry;
		struct buffer_head *bh;
		*first_alloc = myfs_balloc(sb);
		if (*first_alloc == 0)
		{
			*first_alloc = *data_block = 0;
			return -ENOSPC;
		}
		middle = myfs_balloc(sb);
		if (middle == 0)
		{
			myfs_bfree(sb, *first_alloc);
			*first_alloc = *data_block = 0;
			return -ENOSPC;
		}
		*data_block = myfs_balloc_and_scrub(sb);
		if (*data_block == 0)
		{
			myfs_bfree(sb, *first_alloc);
			myfs_bfree(sb, middle);
			*first_alloc = *data_block = 0;
			return -ENOSPC;
		}
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, middle, chain[1]);
		bh = sb_bread(inode->i_sb, middle);
		if (!bh)
		{
			myfs_bfree(sb, *data_block);
			myfs_bfree(sb, middle);
			myfs_bfree(sb, *first_alloc);
			*first_alloc = *data_block = 0;
			return -EIO;
		}
		myfs_scrub_block(bh->b_data);
		entry = (__le32 *)bh->b_data;
		entry += pentry_no;
		*entry = *data_block;
		brelse(bh);
		pentry_no = transposition_cipher(MYFS_I(inode)->key, inode->i_ino, first_alloc, chain[0]);
		bh = sb_bread(inode->i_sb, *first_alloc);
		if (!bh)
		{
			myfs_bfree(sb, *data_block);
			myfs_bfree(sb, middle);
			myfs_bfree(sb, *first_alloc);
			*first_alloc = *data_block = 0;
			return -EIO;
		}
		myfs_scrub_block(bh->b_data);
		entry = (__le32 *)bh->b_data;
		entry += pentry_no;
		*entry = middle;
		brelse(bh);
		return 0;
	}
	return -1;
}

int myfs_logical_to_physical(struct inode *inode, sector_t log, sector_t *phy, int create, int *new)
{
	int err, dep, alloc_to_comp;
	struct super_block *sb;
	uint chain[MYFS_MAX_INDEX_DEPTH];
	sb = inode->i_sb;
	sector_t block_number;
	if (log >= MYFS_MAX_FILE_BLOCKS || log > (i_size_read(inode) >> MYFS_BLOCK_SIZE_BITS))
		return -EFBIG;
	/*
	// todo find block number for ith block of file
	 */
	block_number = 0;
	dep = lblk_to_lindex(log, chain);
	if (dep <= 0)
		return -EINVAL;
	alloc_to_comp = 0;
	err = lindex_to_pblock(inode, &block_number, chain, dep, &alloc_to_comp);
	if (err)
		return err;
	if (alloc_to_comp > 0)
	{
		sector_t first_alloc = 0;
		sector_t last_already_alloc;
		if (!create)
			return -EINVAL;
		/*
		 //todo if the block has not been allocated, and 'create' is set, allocate new block and get its block number
		 */
		last_already_alloc = block_number;
		err = allocate_blocks_with_index(inode, alloc_to_comp, &first_alloc, &block_number, chain + dep - alloc_to_comp + 1);
		if (err)
			return err;
		if (alloc_to_comp == dep)
			/*
			 * when everything beyond inode was allocated now.
			 */
			MYFS_I(inode)->data[chain[0]] = first_alloc;
		else
		{
			/*
			 * new entry is to be put into an index block
			 */
			struct buffer_head *bh2;
			__le32 *entry;
			bh2 = sb_bread(sb, last_already_alloc);
			if (!bh2)
			{
				if (alloc_to_comp > 1)
					free_index(sb, first_alloc, alloc_to_comp - 1);
				myfs_bfree(sb, first_alloc);
				return -EIO;
			}
			entry = (__le32 *)bh2->b_data;
			entry += transposition_cipher(MYFS_I(inode)->key, inode->i_ino, last_already_alloc, chain[dep - alloc_to_comp]);
			*entry = first_alloc;
			inode->i_blocks++;
			brelse(bh2);
		}
		*new = 1;
	}
	*phy = block_number;
	return 0;
}

/*
 * Map buffer head passed in arguement with the ith block of the file represented by the inode
 */
static int myfs_get_ith_block(struct inode *inode, sector_t i, struct buffer_head *bh, int create)
{
	int err = 0;
	int new = 0, boundary = 0;
	sector_t block_number;
	if (i >= MYFS_MAX_FILE_BLOCKS || i > (i_size_read(inode) >> MYFS_BLOCK_SIZE_BITS))
	{
		err = -EFBIG;
		goto end;
	}
	if (i == (i_size_read(inode) >> MYFS_BLOCK_SIZE_BITS))
		boundary = 1;
	err = myfs_logical_to_physical(inode, i, &block_number, create, &new);
	map_bh(bh, inode->i_sb, block_number);
	if (new)
		set_buffer_new(bh);
	if (boundary)
		set_buffer_boundary(bh);
end:
	return err;
}
/*
static sector_t page_to_block_no(struct page *page); //! to be implemented
*/

static inline void get_page_details(struct page *page, struct inode *inode, void **block /* , sector_t *block_no */, loff_t *start, loff_t *end, unsigned long *ino)
{
#if PAGE_SIZE == MYFS_BLOCK_SIZE
	/*
	 * block no = frame number in the bdev as bdev is for volume and 1 page = 1 block
	 */
	*ino = inode->i_ino;
	/* 	*block_no = page_to_block_no(page); */
	*start = page_offset(page) << PAGE_SHIFT;
	*end = i_size_read(inode);
	if (end > start + PAGE_SIZE)
		end = start + PAGE_SIZE;
#else
	/*
	 ! case when block size != page size. implementation not done
	 */
	you cannot compile this code
#endif
}

/*
 * read page from storage to mem. Also decrypts read page
 */
static int myfs_readpage(struct file *file, struct page *page)
{
#if PAGE_SIZE == MYFS_BLOCK_SIZE
	int err = 0;
	err = mpage_readpage(page, myfs_get_ith_block);
	if (!err && !is_zero_pfn(page_to_pfn(page)))
	{
		void *block = NULL;
		/* sector_t block_no; */
		unsigned long ino;
		struct myfs_key *fkey;
		loff_t start, end;
		block = page_address(page);
		fkey = &MYFS_I(file_inode(file))->key;
		get_page_details(page, file_inode(file), &block, /* &block_no, */ &start, &end, &ino);
		chunk_decrypt(block, fkey, /* block_no, */ ino, start, end);
	}
	return err;
#else
	you cannot compile this code
#endif
}

/*
 * write page back to storage. Encrypt while writing
 */
static int myfs_writepage(struct page *page, struct writeback_control *wbc)
{
#if PAGE_SIZE == MYFS_BLOCK_SIZE
	if (!is_zero_pfn(page_to_pfn(page)))
	{
		/* sector_t block_no; */
		loff_t start, end;
		unsigned long ino;
		struct myfs_key *fkey;
		struct address_space *mapping;
		void *block;
		mapping = page_mapping(page);
		if (IS_ERR(mapping))
			return PTR_ERR(mapping);
		block = page_address(page);
		fkey = &MYFS_I(mapping->host)->key;
		get_page_details(page, mapping->host, &block, /* &block_no, */ &start, &end, &ino);
		chunk_encrypt(block, fkey, /* block_no, */ ino, start, end);
	}
	return block_write_full_page(page, myfs_get_ith_block, wbc);
#else
	you cannot compile this code
#endif
}

static void __myfs_truncate_remove_single_indirect(struct inode *inode)
{
	struct super_block *sb;
	struct myfs_incore_inode *incore;
	int p, i;
	sb = inode->i_sb;
	incore = MYFS_I(inode);
	p = MYFS_DIR;
	for (i = 0; i < MYFS_SINGLE_INDIR; i++)
	{
		sector_t bno;
		bno = incore->data[p];
		incore->data[p++] = 0;
		if (bno == 0)
			continue;
		inode->i_blocks -= free_index(sb, bno, 1);
		mark_inode_dirty(inode);
		myfs_bfree(sb, bno);
	}
}

static void __myfs_truncate_remove_double_indirect(struct inode *inode)
{
	struct super_block *sb;
	struct myfs_incore_inode *incore = MYFS_I(inode);
	int p, i;
	sb = inode->i_sb;
	p = MYFS_DIR + MYFS_SINGLE_INDIR;
	for (i = 0; i < MYFS_DOUBLE_INDIR; i++)
	{
		sector_t bno;
		bno = incore->data[p];
		incore->data[p++] = 0;
		if (bno == 0)
			continue;
		inode->i_blocks -= free_index(sb, bno, 2);
		mark_inode_dirty(inode);
		myfs_bfree(sb, bno);
	}
}

/*
 * truncate blocks so that size matches loff_t
 */
void __myfs_truncate_blocks(struct inode *inode, loff_t size)
{
	struct super_block *sb;
	struct myfs_incore_inode *incore;
	int si = MYFS_DIR, di = MYFS_DIR + MYFS_SINGLE_INDIR;
	int i, d;
	ino_t ino;
	uint chain[MYFS_MAX_INDEX_DEPTH];
	incore = MYFS_I(inode);
	sb = inode->i_sb;
	if (size == 0)
	{
		for (i = 0; i < si; i++)
		{
			if (incore->data[i])
			{
				myfs_bfree(sb, incore->data[i]);
				inode->i_blocks--;
				incore->data[i] = 0;
			}
		}
		__myfs_truncate_remove_single_indirect(inode);
		__myfs_truncate_remove_double_indirect(inode);
		inode->i_blocks = 0;
		mark_inode_dirty(inode);
		return;
	}
	d = lblk_to_lindex((size - 1) / MYFS_BLOCK_SIZE, chain);
	ino = inode->i_ino;
	if (d == 1)
	{
		__myfs_truncate_remove_double_indirect(inode);
		__myfs_truncate_remove_single_indirect(inode);
		for (i = chain[0] + 1; i < si; i++)
		{
			sector_t bno;
			bno = incore->data[i];
			incore->data[i] = 0;
			if (bno == 0)
				continue;
			myfs_bfree(sb, bno);
			inode->i_blocks--;
			mark_inode_dirty(inode);
		}
	}
	else if (d == 2)
	{
		sector_t bno = 0, ibno;
		struct buffer_head *bh;
		__myfs_truncate_remove_double_indirect(inode);
		for (i = chain[0] + 1; i < di; i++)
		{
			bno = incore->data[i];
			incore->data[i] = 0;
			if (bno == 0)
				continue;
			inode->i_blocks -= free_index(sb, bno, 1);
			mark_inode_dirty(inode);
			myfs_bfree(sb, bno);
		}
		bno = incore->data[chain[0]];
		if (bno == 0)
			return;
		ibno = bno;
		bh = sb_bread(sb, ibno);
		if (bh)
		{
			__le32 *index;
			index = (__le32 *)bh->b_data;
			for (i = chain[1] + 1; i < MYFS_POINTERS_PERBLOCK; i++)
			{
				int pentry;
				pentry = transposition_cipher(MYFS_I(inode)->key, ino, ibno, i);
				bno = index[pentry];
				index[pentry] = 0;
				if (bno == 0)
					continue;
				myfs_bfree(sb, bno);
				inode->i_blocks--;
				mark_inode_dirty(inode);
			}
			brelse(bh);
		}
	}
	else if (d == 3)
	{
		sector_t bno = 0, ibno;
		struct buffer_head *bh;
		for (i = chain[0] + 1; i < MYFS_NUM_POINTERS; i++)
		{
			bno = incore->data[i];
			incore->data[i] = 0;
			if (bno == 0)
				continue;
			inode->i_blocks -= free_index(sb, bno, 2);
			mark_inode_dirty(inode);
			myfs_bfree(sb, bno);
		}
		bno = incore->data[chain[0]];
		if (bno == 0)
			return;
		ibno = bno;
		bh = sb_bread(sb, ibno);
		if (bh)
		{
			__le32 *index;
			index = (__le32 *)bh->b_data;
			for (i = chain[1] + 1; i < MYFS_POINTERS_PERBLOCK; i++)
			{
				int pentry = transposition_cipher(MYFS_I(inode)->key, ino, ibno, i);
				bno = index[pentry];
				index[pentry] = 0;
				if (bno == 0)
					continue;
				inode->i_blocks -= free_index(sb, bno, 1);
				mark_inode_dirty(inode);
				myfs_bfree(sb, bno);
			}
			bno = index[transposition_cipher(MYFS_I(inode)->key, ino, ibno, chain[1])];
			brelse(bh);
			if (bno == 0)
				return;
			ibno = bno;
			bh = sb_bread(sb, ibno);
			if (bh)
			{
				for (i = chain[2] + 1; i < MYFS_POINTERS_PERBLOCK; i++)
				{
					int pentry = transposition_cipher(MYFS_I(inode)->key, ino, ibno, i);
					bno = index[pentry];
					index[pentry] = 0;
					if (bno == 0)
						continue;
					myfs_bfree(sb, bno);
					inode->i_blocks--;
					mark_inode_dirty(inode);
				}
				brelse(bh);
			}
		}
	}
}

/*
 * free blocks (if any) from size onwards
 */
void myfs_truncate_blocks(struct inode *inode, loff_t size)
{
	filemap_invalidate_lock(inode->i_mapping);
	__myfs_truncate_blocks(inode, size);
	filemap_invalidate_unlock(inode->i_mapping);
}

/*
 * called when when write fails
 */
static void myfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode;
	loff_t size;
	inode = mapping->host;
	size = i_size_read(inode);
	if (to > size)
	{
		truncate_pagecache(inode, size);   /* truncates page cache only. doesn't affect physical blocks*/
		myfs_truncate_blocks(inode, size); /* truncates physical blocks*/
	}
}

static int myfs_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int flags, struct page **pagep, void **fsdata)
{
	int ret;
	ret = block_write_begin(mapping, pos, len, flags, pagep, myfs_get_ith_block);
	if (ret < 0)
		myfs_write_failed(mapping, pos + len); /* will truncate and free blocks that were allocated but cannot be used as write failed
												*/
	return ret;
}

static int myfs_write_end(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int copied, struct page *page, void *fsdata)
{
	int ret;
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len)
		myfs_write_failed(mapping, pos + len);
	return ret;
}

struct address_space_operations myfs_file_asops = {
	.readpage = myfs_readpage,
	.writepage = myfs_writepage,
	.write_begin = myfs_write_begin,
	.write_end = myfs_write_end,
};

static int myfs_open(struct inode *inode, struct file *filep)
{
	struct dentry *dentry;
	unsigned long dentry_flags;
	if (!S_ISREG(inode->i_mode))
		return -ENOTSUPP;
	dentry = file_dentry(filep);
	dentry_flags = dentry_get_myfsflags(dentry);
	if (!TEST_OP(dentry_flags, MYFS_REGACC))
		return -EACCES;
	if (!TEST_OP(MYFS_I(inode)->flags, MYFS_REGACC))
		return -EACCES;
	return 0;
}

struct file_operations myfs_file_ops = {
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.fsync = generic_file_fsync,
	.open = myfs_open,
};
