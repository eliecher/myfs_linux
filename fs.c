#include "myfs.h"
#include "cryp.h"
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/sha1.h>

///@file

static struct dentry *myfs_mount(struct file_system_type *fs_type,
								 int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);
}

static struct file_system_type myfs_fs = {
	.owner = THIS_MODULE,
	.name = "myfs",
	.mount = myfs_mount,		 /*  for mounting */
	.kill_sb = kill_block_super, /*  kernel helper function */
	.fs_flags = FS_REQUIRES_DEV, /*  device based filesystem */
};

static int __init init_myfs_fs(void)
{
	int err = 0;
	err = init_inode_cache();
	if (err)
		goto out;
	err = register_filesystem(&myfs_fs);
	if (err)
		goto destroy_inode_cache;
out:
	return err;
destroy_inode_cache:
	destroy_inode_cache();
	goto out;
}
static void __exit exit_myfs_fs(void)
{
	unregister_filesystem(&myfs_fs);
	destroy_inode_cache();
}

MODULE_LICENSE("GPL");
module_init(init_myfs_fs);
module_exit(exit_myfs_fs);
