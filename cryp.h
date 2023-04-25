#ifndef MYFS_CRYP_H
#define MYFS_CRYP_H
#include <linux/types.h>
#include <linux/string.h>

#define MYFS_PASS_SEP '~'
#define MYFS_PASS_MAX_LEN 64
#define MYFS_HASH_LEN 20
#define MYFS_KEY_LEN 16

struct myfs_pass_hash
{
	unsigned char hash[MYFS_HASH_LEN];
};


struct myfs_key
{
	unsigned char key[MYFS_KEY_LEN];
};


extern void passwd_to_hash(const char *passwd, int len, struct myfs_pass_hash *hash);
extern void passwd_to_key(const char *passwd, int len, struct myfs_key *key);
extern void combine_keys(const struct myfs_key *to_add, struct myfs_key *add_to);

extern int transposition_cipher(struct myfs_key fkey,unsigned long i_no, unsigned long long block_no, __u16 lentry_no);

/*
 ! due to difficulty in acquiring block_no, temporarily chunk encryption/decryption is to be done w/o it
 */
extern void chunk_encrypt(void *block, struct myfs_key *key, /* sector_t block_no, */ __u32 ino, long long start_off, long long end_off);
extern void chunk_decrypt(void *block, struct myfs_key *key, /* sector_t block_no, */ __u32 ino, long long start_off, long long end_off);
#endif