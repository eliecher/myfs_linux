#include "cryp.h"
///@file

static void sha1_transform(__u32 state[5], const unsigned char buffer[64]);
static void md5_transform(__u32 state[4], const unsigned char buffer[64]);

/*
 * calculate hash of a password
 */
void passwd_to_hash(const char *passwd, int len, struct myfs_pass_hash *hash)
{
	/*
	 * uses SHA1 hash
	 */
	__u32 state[5] = {
		0x67452301,
		0xEFCDAB89,
		0x98BADCFE,
		0x10325476,
		0xC3D2E1F0};
	unsigned char buffer[64];
	int done = 0;
	int i;
	for (done = 0; done + 64 <= len; done += 64)
	{
		memcpy(buffer, passwd + done, 64);
		sha1_transform(state, buffer);
	}
	memset(buffer, 0, 64);
	if (len - done > 55)
	{
		memcpy(buffer, passwd + done, len - done);
		buffer[len - done] = 0x80;
		sha1_transform(state, buffer);
		memset(buffer, 0, 64);
	}
	else
	{
		memcpy(buffer, passwd + done, len - done);
	}
	buffer[56] = (((__u64)len) >> 56);
	buffer[57] = (((__u64)len) >> 48);
	buffer[58] = (((__u64)len) >> 40);
	buffer[59] = (((__u64)len) >> 32);
	buffer[60] = (((__u64)len) >> 24);
	buffer[61] = (((__u64)len) >> 16);
	buffer[62] = (((__u64)len) >> 8);
	buffer[63] = (((__u64)len) >> 0);
	sha1_transform(state, buffer);
	for (i = 0; i < 5; i++)
	{
		hash->hash[i * 4 + 0] = (state[i] >> 24) & 0xff;
		hash->hash[i * 4 + 1] = (state[i] >> 16) & 0xff;
		hash->hash[i * 4 + 2] = (state[i] >> 8) & 0xff;
		hash->hash[i * 4 + 3] = (state[i] >> 0) & 0xff;
	}
}

/*
 * calculate key from a password
 */
void passwd_to_key(const char *passwd, int len, struct myfs_key *key)
{
	/*
	 * uses MD5 hash
	 */
	__u32 state[4] = {
		0x67452301,
		0xefcdab89,
		0x98badcfe,
		0x10325476,
	};
	unsigned char buffer[64];
	int done = 0;
	int i;
	for (done = 0; done + 64 <= len; done += 64)
	{
		memcpy(buffer, passwd + done, 64);
		md5_transform(state, buffer);
	}
	memset(buffer, 0, 64);
	if (len - done > 55)
	{
		memcpy(buffer, passwd + done, len - done);
		buffer[len - done] = 0x80;
		md5_transform(state, buffer);
		memset(buffer, 0, 64);
	}
	else
	{
		memcpy(buffer, passwd + done, len - done);
	}
	buffer[56] = (((__u64)len) >> 56);
	buffer[57] = (((__u64)len) >> 48);
	buffer[58] = (((__u64)len) >> 40);
	buffer[59] = (((__u64)len) >> 32);
	buffer[60] = (((__u64)len) >> 24);
	buffer[61] = (((__u64)len) >> 16);
	buffer[62] = (((__u64)len) >> 8);
	buffer[63] = (((__u64)len) >> 0);
	md5_transform(state, buffer);

	for (i = 0; i < 4; i++)
	{
		key->key[i * 4] = (unsigned char)(state[i] & 0xff);
		key->key[i * 4 + 1] = (unsigned char)((state[i] >> 8) & 0xff);
		key->key[i * 4 + 2] = (unsigned char)((state[i] >> 16) & 0xff);
		key->key[i * 4 + 3] = (unsigned char)((state[i] >> 24) & 0xff);
	}
}

static void sha1_transform(__u32 state[5], const unsigned char buffer[64])
{
	__u32 a, b, c, d, e, f, k, temp;
	__u32 w[80];
	int i;

	/* initialize the message schedule */
	for (i = 0; i < 16; i++)
	{
		w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) |
			   (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
	}
	for (i = 16; i < 80; i++)
	{
		w[i] = (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]);
		w[i] = (w[i] << 1) | (w[i] >> 31);
	}

	/* initialize the hash value */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];

	/* main loop */
	for (i = 0; i < 80; i++)
	{
		if (i < 20)
		{
			f = (b & c) | ((~b) & d);
			k = 0x5A827999;
		}
		else if (i < 40)
		{
			f = b ^ c ^ d;
			k = 0x6ED9EBA1;
		}
		else if (i < 60)
		{
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDC;
		}
		else
		{
			f = b ^ c ^ d;
			k = 0xCA62C1D6;
		}
		temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
		e = d;
		d = c;
		c = ((b << 30) | (b >> 2));
		b = a;
		a = temp;
	}

	/* update the hash value */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

/* Rotate left function */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* MD5 constants and functions */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* MD5 round functions */
#define FF(a, b, c, d, x, s, ac)                     \
	{                                                \
		(a) += F((b), (c), (d)) + (x) + (__u32)(ac); \
		(a) = ROTATE_LEFT((a), (s));                 \
		(a) += (b);                                  \
	}
#define GG(a, b, c, d, x, s, ac)                     \
	{                                                \
		(a) += G((b), (c), (d)) + (x) + (__u32)(ac); \
		(a) = ROTATE_LEFT((a), (s));                 \
		(a) += (b);                                  \
	}
#define HH(a, b, c, d, x, s, ac)                     \
	{                                                \
		(a) += H((b), (c), (d)) + (x) + (__u32)(ac); \
		(a) = ROTATE_LEFT((a), (s));                 \
		(a) += (b);                                  \
	}
#define II(a, b, c, d, x, s, ac)                     \
	{                                                \
		(a) += I((b), (c), (d)) + (x) + (__u32)(ac); \
		(a) = ROTATE_LEFT((a), (s));                 \
		(a) += (b);                                  \
	}

static void md5_transform(__u32 state[4], const unsigned char buffer[64])
{

	__u32 a = state[0];
	__u32 b = state[1];
	__u32 c = state[2];
	__u32 d = state[3];
	__u32 x[16];
	int i;
	i = 0;
	/* Convert the input buffer to an array of 32-bit words */
	for (i = 0; i < 16; i++)
	{
		x[i] = (__u32)buffer[i * 4] | ((__u32)buffer[i * 4 + 1] << 8) |
			   ((__u32)buffer[i * 4 + 2] << 16) | ((__u32)buffer[i * 4 + 3] << 24);
	}
	FF(a, b, c, d, x[0], 7, 0xd76aa478);   /* 1 */
	FF(d, a, b, c, x[1], 12, 0xe8c7b756);  /* 2 */
	FF(c, d, a, b, x[2], 17, 0x242070db);  /* 3 */
	FF(b, c, d, a, x[3], 22, 0xc1bdceee);  /* 4 */
	FF(a, b, c, d, x[4], 7, 0xf57c0faf);   /* 5 */
	FF(d, a, b, c, x[5], 12, 0x4787c62a);  /* 6 */
	FF(c, d, a, b, x[6], 17, 0xa8304613);  /* 7 */
	FF(b, c, d, a, x[7], 22, 0xfd469501);  /* 8 */
	FF(a, b, c, d, x[8], 7, 0x698098d8);   /* 9 */
	FF(d, a, b, c, x[9], 12, 0x8b44f7af);  /* 10 */
	FF(c, d, a, b, x[10], 17, 0xffff5bb1); /* 11 */
	FF(b, c, d, a, x[11], 22, 0x895cd7be); /* 12 */
	FF(a, b, c, d, x[12], 7, 0x6b901122);  /* 13 */
	FF(d, a, b, c, x[13], 12, 0xfd987193); /* 14 */
	FF(c, d, a, b, x[14], 17, 0xa679438e); /* 15 */
	FF(b, c, d, a, x[15], 22, 0x49b40821); /* 16 */

	/* Round 2 */
	GG(a, b, c, d, x[1], 5, 0xf61e2562);   /* 17 */
	GG(d, a, b, c, x[6], 9, 0xc040b340);   /* 18 */
	GG(c, d, a, b, x[11], 14, 0x265e5a51); /* 19 */
	GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);  /* 20 */
	GG(a, b, c, d, x[5], 5, 0xd62f105d);   /* 21 */
	GG(d, a, b, c, x[10], 9, 0x2441453);   /* 22 */
	GG(c, d, a, b, x[15], 14, 0xd8a1e681); /* 23 */
	GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);  /* 24 */
	GG(a, b, c, d, x[9], 5, 0x21e1cde6);   /* 25 */
	GG(d, a, b, c, x[14], 9, 0xc33707d6);  /* 26 */
	GG(c, d, a, b, x[3], 14, 0xf4d50d87);  /* 27 */
	GG(b, c, d, a, x[8], 20, 0x455a14ed);  /* 28 */
	GG(a, b, c, d, x[13], 5, 0xa9e3e905);  /* 29 */
	GG(d, a, b, c, x[2], 9, 0xfcefa3f8);   /* 30 */
	GG(c, d, a, b, x[7], 14, 0x676f02d9);  /* 31 */
	GG(b, c, d, a, x[12], 20, 0x8d2a4c8a); /* 32 */

	/* Round 3 */
	HH(a, b, c, d, x[5], 4, 0xfffa3942);   /* 33 */
	HH(d, a, b, c, x[8], 11, 0x8771f681);  /* 34 */
	HH(c, d, a, b, x[11], 16, 0x6d9d6122); /* 35 */
	HH(b, c, d, a, x[14], 23, 0xfde5380c); /* 36 */
	HH(a, b, c, d, x[1], 4, 0xa4beea44);   /* 37 */
	HH(d, a, b, c, x[4], 11, 0x4bdecfa9);  /* 38 */
	HH(c, d, a, b, x[7], 16, 0xf6bb4b60);  /* 39 */
	HH(b, c, d, a, x[10], 23, 0xbebfbc70); /* 40 */
	HH(a, b, c, d, x[13], 4, 0x289b7ec6);  /* 41 */
	HH(d, a, b, c, x[0], 11, 0xeaa127fa);  /* 42 */
	HH(c, d, a, b, x[3], 16, 0xd4ef3085);  /* 43 */
	HH(b, c, d, a, x[6], 23, 0x4881d05);   /* 44 */
	HH(a, b, c, d, x[9], 4, 0xd9d4d039);   /* 45 */
	HH(d, a, b, c, x[12], 11, 0xe6db99e5); /* 46 */
	HH(c, d, a, b, x[15], 16, 0x1fa27cf8); /* 47 */
	HH(b, c, d, a, x[2], 23, 0xc4ac5665);  /* 48 */

	/* Round 4 */
	II(a, b, c, d, x[0], 6, 0xf4292244);   /* 49 */
	II(d, a, b, c, x[7], 10, 0x432aff97);  /* 50 */
	II(c, d, a, b, x[14], 15, 0xab9423a7); /* 51 */
	II(b, c, d, a, x[5], 21, 0xfc93a039);  /* 52 */
	II(a, b, c, d, x[12], 6, 0x655b59c3);  /* 53 */
	II(d, a, b, c, x[3], 10, 0x8f0ccc92);  /* 54 */
	II(c, d, a, b, x[10], 15, 0xffeff47d); /* 55 */
	II(b, c, d, a, x[1], 21, 0x85845dd1);  /* 56 */
	II(a, b, c, d, x[8], 6, 0x6fa87e4f);   /* 57 */
	II(d, a, b, c, x[15], 10, 0xfe2ce6e0); /* 58 */
	II(c, d, a, b, x[6], 15, 0xa3014314);  /* 59 */
	II(b, c, d, a, x[13], 21, 0x4e0811a1); /* 60 */
	II(a, b, c, d, x[4], 6, 0xf7537e82);   /* 61 */
	II(d, a, b, c, x[11], 10, 0xbd3af235); /* 62 */
	II(c, d, a, b, x[2], 15, 0x2ad7d2bb);  /* 63 */
	II(b, c, d, a, x[9], 21, 0xeb86d391);  /* 64 */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}

typedef union
{
	__u8 w[4];
	__u32 i;
} word;

static const __u8 aes_sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

static const __u8 rcon[11] = {
	0b00000000,
	0b00000001,
	0b00000010,
	0b00000100,
	0b00001000,
	0b00010000,
	0b00100000,
	0b01000000,
	0b10000000,
	0b00011011,
	0b00110110,
};

static inline word aes_sub_word(word w)
{

	return (word){.w = {
					  aes_sbox[w.w[0]],
					  aes_sbox[w.w[1]],
					  aes_sbox[w.w[2]],
					  aes_sbox[w.w[3]],
				  }};
}

static inline word aes_rot_word(word w)
{
	return (word){.w = {
					  w.w[1],
					  w.w[2],
					  w.w[3],
					  w.w[0],
				  }};
}

static inline word aes_xor(const word w1, const word w2)
{
	return (word){.i = w1.i ^ w2.i};
}

static void aes_expand_key(const __u8 key[16], word w[44])
{
	word temp;
	int i;
	for (i = 0; i < 4; i++)
		w[i] = *(word *)(key + 4 * i);
	for (i = 4; i < 44; i += 4)
	{
		temp = w[i - 1];
		temp = aes_rot_word(temp);
		temp = aes_sub_word(temp);
		temp.w[0] ^= rcon[i / 4];
		w[i] = aes_xor(temp, w[i - 4]);
		w[i + 1] = aes_xor(w[i], w[i - 3]);
		w[i + 2] = aes_xor(w[i + 1], w[i - 2]);
		w[i + 3] = aes_xor(w[i + 2], w[i - 1]);
	}
}

static inline void aes_add_round_key(word state[4], const word rkey[4])
{
	state[0] = aes_xor(state[0], rkey[0]);
	state[1] = aes_xor(state[1], rkey[1]);
	state[2] = aes_xor(state[2], rkey[2]);
	state[3] = aes_xor(state[3], rkey[3]);
}

static inline void aes_byte_sub(word state[4])
{
	state[0] = aes_sub_word(state[0]);
	state[1] = aes_sub_word(state[1]);
	state[2] = aes_sub_word(state[2]);
	state[3] = aes_sub_word(state[3]);
}

static inline void aes_shift_rows(word state[4])
{
#define SWAP(a, b) ((a) = ((a) + (b)) - ((b) = (a)))
	__u8 t;
	t = state[0].w[1];
	state[0].w[1] = state[1].w[1];
	state[1].w[1] = state[2].w[1];
	state[2].w[1] = state[3].w[1];
	state[3].w[1] = t;
	SWAP(state[0].w[2], state[2].w[2]);
	SWAP(state[1].w[2], state[3].w[2]);
	t = state[3].w[3];
	state[3].w[3] = state[2].w[3];
	state[2].w[3] = state[1].w[3];
	state[1].w[3] = state[0].w[3];
	state[0].w[3] = t;
#undef SWAP
}
static inline word aes_mult_col(word col)
{
	return (word){
		.w = {
			2 * col.w[0] + 3 * col.w[1] + 1 * col.w[2] + 1 * col.w[3],
			1 * col.w[0] + 2 * col.w[1] + 3 * col.w[2] + 1 * col.w[3],
			1 * col.w[0] + 1 * col.w[1] + 2 * col.w[2] + 3 * col.w[3],
			3 * col.w[0] + 1 * col.w[1] + 1 * col.w[2] + 2 * col.w[3],
		}};
}

static inline void aes_mix_columns(word state[4])
{
	state[0] = aes_mult_col(state[0]);
	state[1] = aes_mult_col(state[1]);
	state[2] = aes_mult_col(state[2]);
	state[3] = aes_mult_col(state[3]);
}

static inline void aes_encrypt(__u8 block[16], word round_keys[44])
{
	word state[4];
	int i, j;
	int round;
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			state[i].w[j] = block[i * 4 + j];
	aes_add_round_key(state, round_keys);
	for (round = 1; round < 10; round++)
	{
		aes_byte_sub(state);
		aes_shift_rows(state);
		aes_mix_columns(state);
		aes_add_round_key(state, round_keys + round * 4);
	}
	aes_byte_sub(state);
	aes_shift_rows(state);
	aes_add_round_key(state, round_keys + 40);
	for (i = 0; i < 4; i++)
		for (j = 0; j < 4; j++)
			block[i * 4 + j] = state[i].w[j];
}

typedef __u8 chunk[16];
void chunk_encrypt(void *block, struct myfs_key *key, /* sector_t block_no, */ __u32 ino, long long start_off, long long end_off)
{
	word keys[44];
	__u8 buffer[16];
	int n_chunks;
	chunk *chunks;
	int i, j, k;
	long long s;
	aes_expand_key(key->key, keys);
	if (
#ifdef __KERNEL__
		unlikely(
#else
		(
#endif
			start_off % 16))
	{
		s = start_off - start_off % 16;
		memset(buffer, 0, 16);
		/* store ino in big endian format */
		buffer[0] = ino >> 24;
		buffer[1] = ino >> 16;
		buffer[2] = ino >> 8;
		buffer[3] = ino;
		/* store start_off in little endian format */
		buffer[15] = s >> 56;
		buffer[14] = s >> 48;
		buffer[13] = s >> 40;
		buffer[12] = s >> 32;
		buffer[11] = s >> 24;
		buffer[10] = s >> 16;
		buffer[9] = s >> 8;
		buffer[8] = s;
		aes_encrypt(buffer, keys);
		for (j = start_off % 16, k = 0; j < 16; j++, k++)
			*((char *)block) ^= buffer[j];
		block += 16 - start_off % 16;
		start_off = s + 16;
	}
	n_chunks = (end_off - start_off) / 16;
	chunks = (chunk*)(block);
	for (i = 0; i < n_chunks; i++)
	{
		memset(buffer, 0, 16);
		/* store ino in big endian format */
		buffer[0] = ino >> 24;
		buffer[1] = ino >> 16;
		buffer[2] = ino >> 8;
		buffer[3] = ino;
		/* store start_off in little endian format */
		buffer[15] = start_off >> 56;
		buffer[14] = start_off >> 48;
		buffer[13] = start_off >> 40;
		buffer[12] = start_off >> 32;
		buffer[11] = start_off >> 24;
		buffer[10] = start_off >> 16;
		buffer[9] = start_off >> 8;
		buffer[8] = start_off;
		aes_encrypt(buffer, keys);
		for (j = 0; j < 16; j++)
			chunks[i][j] ^= buffer[j];
		start_off += 16;
	}

	if (start_off != end_off)
	{
		memset(buffer, 0, 16);
		/* store ino in big endian format */
		buffer[0] = ino >> 24;
		buffer[1] = ino >> 16;
		buffer[2] = ino >> 8;
		buffer[3] = ino;
		/* store start_off in little endian format */
		buffer[15] = start_off >> 56;
		buffer[14] = start_off >> 48;
		buffer[13] = start_off >> 40;
		buffer[12] = start_off >> 32;
		buffer[11] = start_off >> 24;
		buffer[10] = start_off >> 16;
		buffer[9] = start_off >> 8;
		buffer[8] = start_off;
		aes_encrypt(buffer, keys);
		for (j = 0; j < end_off - start_off; j++)
			chunks[n_chunks][j] ^= buffer[j];
		start_off = end_off;
	}
	return;
}
void chunk_decrypt(void *block, struct myfs_key *key, /* sector_t block_no, */ __u32 ino, long long start_off, long long end_off)
{
	chunk_encrypt(block, key, /* block_no, */ ino, start_off, end_off);
}

static const __u16 spn_pbox[1024] = {
	0x0000,
	0x0001,
	0x0008,
	0x0009,
	0x0010,
	0x0011,
	0x0018,
	0x0019,
	0x0040,
	0x0041,
	0x0048,
	0x0049,
	0x0050,
	0x0051,
	0x0058,
	0x0059,
	0x0100,
	0x0101,
	0x0108,
	0x0109,
	0x0110,
	0x0111,
	0x0118,
	0x0119,
	0x0140,
	0x0141,
	0x0148,
	0x0149,
	0x0150,
	0x0151,
	0x0158,
	0x0159,
	0x0002,
	0x0003,
	0x000a,
	0x000b,
	0x0012,
	0x0013,
	0x001a,
	0x001b,
	0x0042,
	0x0043,
	0x004a,
	0x004b,
	0x0052,
	0x0053,
	0x005a,
	0x005b,
	0x0102,
	0x0103,
	0x010a,
	0x010b,
	0x0112,
	0x0113,
	0x011a,
	0x011b,
	0x0142,
	0x0143,
	0x014a,
	0x014b,
	0x0152,
	0x0153,
	0x015a,
	0x015b,
	0x0004,
	0x0005,
	0x000c,
	0x000d,
	0x0014,
	0x0015,
	0x001c,
	0x001d,
	0x0044,
	0x0045,
	0x004c,
	0x004d,
	0x0054,
	0x0055,
	0x005c,
	0x005d,
	0x0104,
	0x0105,
	0x010c,
	0x010d,
	0x0114,
	0x0115,
	0x011c,
	0x011d,
	0x0144,
	0x0145,
	0x014c,
	0x014d,
	0x0154,
	0x0155,
	0x015c,
	0x015d,
	0x0006,
	0x0007,
	0x000e,
	0x000f,
	0x0016,
	0x0017,
	0x001e,
	0x001f,
	0x0046,
	0x0047,
	0x004e,
	0x004f,
	0x0056,
	0x0057,
	0x005e,
	0x005f,
	0x0106,
	0x0107,
	0x010e,
	0x010f,
	0x0116,
	0x0117,
	0x011e,
	0x011f,
	0x0146,
	0x0147,
	0x014e,
	0x014f,
	0x0156,
	0x0157,
	0x015e,
	0x015f,
	0x0020,
	0x0021,
	0x0028,
	0x0029,
	0x0030,
	0x0031,
	0x0038,
	0x0039,
	0x0060,
	0x0061,
	0x0068,
	0x0069,
	0x0070,
	0x0071,
	0x0078,
	0x0079,
	0x0120,
	0x0121,
	0x0128,
	0x0129,
	0x0130,
	0x0131,
	0x0138,
	0x0139,
	0x0160,
	0x0161,
	0x0168,
	0x0169,
	0x0170,
	0x0171,
	0x0178,
	0x0179,
	0x0022,
	0x0023,
	0x002a,
	0x002b,
	0x0032,
	0x0033,
	0x003a,
	0x003b,
	0x0062,
	0x0063,
	0x006a,
	0x006b,
	0x0072,
	0x0073,
	0x007a,
	0x007b,
	0x0122,
	0x0123,
	0x012a,
	0x012b,
	0x0132,
	0x0133,
	0x013a,
	0x013b,
	0x0162,
	0x0163,
	0x016a,
	0x016b,
	0x0172,
	0x0173,
	0x017a,
	0x017b,
	0x0024,
	0x0025,
	0x002c,
	0x002d,
	0x0034,
	0x0035,
	0x003c,
	0x003d,
	0x0064,
	0x0065,
	0x006c,
	0x006d,
	0x0074,
	0x0075,
	0x007c,
	0x007d,
	0x0124,
	0x0125,
	0x012c,
	0x012d,
	0x0134,
	0x0135,
	0x013c,
	0x013d,
	0x0164,
	0x0165,
	0x016c,
	0x016d,
	0x0174,
	0x0175,
	0x017c,
	0x017d,
	0x0026,
	0x0027,
	0x002e,
	0x002f,
	0x0036,
	0x0037,
	0x003e,
	0x003f,
	0x0066,
	0x0067,
	0x006e,
	0x006f,
	0x0076,
	0x0077,
	0x007e,
	0x007f,
	0x0126,
	0x0127,
	0x012e,
	0x012f,
	0x0136,
	0x0137,
	0x013e,
	0x013f,
	0x0166,
	0x0167,
	0x016e,
	0x016f,
	0x0176,
	0x0177,
	0x017e,
	0x017f,
	0x0080,
	0x0081,
	0x0088,
	0x0089,
	0x0090,
	0x0091,
	0x0098,
	0x0099,
	0x00c0,
	0x00c1,
	0x00c8,
	0x00c9,
	0x00d0,
	0x00d1,
	0x00d8,
	0x00d9,
	0x0180,
	0x0181,
	0x0188,
	0x0189,
	0x0190,
	0x0191,
	0x0198,
	0x0199,
	0x01c0,
	0x01c1,
	0x01c8,
	0x01c9,
	0x01d0,
	0x01d1,
	0x01d8,
	0x01d9,
	0x0082,
	0x0083,
	0x008a,
	0x008b,
	0x0092,
	0x0093,
	0x009a,
	0x009b,
	0x00c2,
	0x00c3,
	0x00ca,
	0x00cb,
	0x00d2,
	0x00d3,
	0x00da,
	0x00db,
	0x0182,
	0x0183,
	0x018a,
	0x018b,
	0x0192,
	0x0193,
	0x019a,
	0x019b,
	0x01c2,
	0x01c3,
	0x01ca,
	0x01cb,
	0x01d2,
	0x01d3,
	0x01da,
	0x01db,
	0x0084,
	0x0085,
	0x008c,
	0x008d,
	0x0094,
	0x0095,
	0x009c,
	0x009d,
	0x00c4,
	0x00c5,
	0x00cc,
	0x00cd,
	0x00d4,
	0x00d5,
	0x00dc,
	0x00dd,
	0x0184,
	0x0185,
	0x018c,
	0x018d,
	0x0194,
	0x0195,
	0x019c,
	0x019d,
	0x01c4,
	0x01c5,
	0x01cc,
	0x01cd,
	0x01d4,
	0x01d5,
	0x01dc,
	0x01dd,
	0x0086,
	0x0087,
	0x008e,
	0x008f,
	0x0096,
	0x0097,
	0x009e,
	0x009f,
	0x00c6,
	0x00c7,
	0x00ce,
	0x00cf,
	0x00d6,
	0x00d7,
	0x00de,
	0x00df,
	0x0186,
	0x0187,
	0x018e,
	0x018f,
	0x0196,
	0x0197,
	0x019e,
	0x019f,
	0x01c6,
	0x01c7,
	0x01ce,
	0x01cf,
	0x01d6,
	0x01d7,
	0x01de,
	0x01df,
	0x00a0,
	0x00a1,
	0x00a8,
	0x00a9,
	0x00b0,
	0x00b1,
	0x00b8,
	0x00b9,
	0x00e0,
	0x00e1,
	0x00e8,
	0x00e9,
	0x00f0,
	0x00f1,
	0x00f8,
	0x00f9,
	0x01a0,
	0x01a1,
	0x01a8,
	0x01a9,
	0x01b0,
	0x01b1,
	0x01b8,
	0x01b9,
	0x01e0,
	0x01e1,
	0x01e8,
	0x01e9,
	0x01f0,
	0x01f1,
	0x01f8,
	0x01f9,
	0x00a2,
	0x00a3,
	0x00aa,
	0x00ab,
	0x00b2,
	0x00b3,
	0x00ba,
	0x00bb,
	0x00e2,
	0x00e3,
	0x00ea,
	0x00eb,
	0x00f2,
	0x00f3,
	0x00fa,
	0x00fb,
	0x01a2,
	0x01a3,
	0x01aa,
	0x01ab,
	0x01b2,
	0x01b3,
	0x01ba,
	0x01bb,
	0x01e2,
	0x01e3,
	0x01ea,
	0x01eb,
	0x01f2,
	0x01f3,
	0x01fa,
	0x01fb,
	0x00a4,
	0x00a5,
	0x00ac,
	0x00ad,
	0x00b4,
	0x00b5,
	0x00bc,
	0x00bd,
	0x00e4,
	0x00e5,
	0x00ec,
	0x00ed,
	0x00f4,
	0x00f5,
	0x00fc,
	0x00fd,
	0x01a4,
	0x01a5,
	0x01ac,
	0x01ad,
	0x01b4,
	0x01b5,
	0x01bc,
	0x01bd,
	0x01e4,
	0x01e5,
	0x01ec,
	0x01ed,
	0x01f4,
	0x01f5,
	0x01fc,
	0x01fd,
	0x00a6,
	0x00a7,
	0x00ae,
	0x00af,
	0x00b6,
	0x00b7,
	0x00be,
	0x00bf,
	0x00e6,
	0x00e7,
	0x00ee,
	0x00ef,
	0x00f6,
	0x00f7,
	0x00fe,
	0x00ff,
	0x01a6,
	0x01a7,
	0x01ae,
	0x01af,
	0x01b6,
	0x01b7,
	0x01be,
	0x01bf,
	0x01e6,
	0x01e7,
	0x01ee,
	0x01ef,
	0x01f6,
	0x01f7,
	0x01fe,
	0x01ff,
	0x0200,
	0x0201,
	0x0208,
	0x0209,
	0x0210,
	0x0211,
	0x0218,
	0x0219,
	0x0240,
	0x0241,
	0x0248,
	0x0249,
	0x0250,
	0x0251,
	0x0258,
	0x0259,
	0x0300,
	0x0301,
	0x0308,
	0x0309,
	0x0310,
	0x0311,
	0x0318,
	0x0319,
	0x0340,
	0x0341,
	0x0348,
	0x0349,
	0x0350,
	0x0351,
	0x0358,
	0x0359,
	0x0202,
	0x0203,
	0x020a,
	0x020b,
	0x0212,
	0x0213,
	0x021a,
	0x021b,
	0x0242,
	0x0243,
	0x024a,
	0x024b,
	0x0252,
	0x0253,
	0x025a,
	0x025b,
	0x0302,
	0x0303,
	0x030a,
	0x030b,
	0x0312,
	0x0313,
	0x031a,
	0x031b,
	0x0342,
	0x0343,
	0x034a,
	0x034b,
	0x0352,
	0x0353,
	0x035a,
	0x035b,
	0x0204,
	0x0205,
	0x020c,
	0x020d,
	0x0214,
	0x0215,
	0x021c,
	0x021d,
	0x0244,
	0x0245,
	0x024c,
	0x024d,
	0x0254,
	0x0255,
	0x025c,
	0x025d,
	0x0304,
	0x0305,
	0x030c,
	0x030d,
	0x0314,
	0x0315,
	0x031c,
	0x031d,
	0x0344,
	0x0345,
	0x034c,
	0x034d,
	0x0354,
	0x0355,
	0x035c,
	0x035d,
	0x0206,
	0x0207,
	0x020e,
	0x020f,
	0x0216,
	0x0217,
	0x021e,
	0x021f,
	0x0246,
	0x0247,
	0x024e,
	0x024f,
	0x0256,
	0x0257,
	0x025e,
	0x025f,
	0x0306,
	0x0307,
	0x030e,
	0x030f,
	0x0316,
	0x0317,
	0x031e,
	0x031f,
	0x0346,
	0x0347,
	0x034e,
	0x034f,
	0x0356,
	0x0357,
	0x035e,
	0x035f,
	0x0220,
	0x0221,
	0x0228,
	0x0229,
	0x0230,
	0x0231,
	0x0238,
	0x0239,
	0x0260,
	0x0261,
	0x0268,
	0x0269,
	0x0270,
	0x0271,
	0x0278,
	0x0279,
	0x0320,
	0x0321,
	0x0328,
	0x0329,
	0x0330,
	0x0331,
	0x0338,
	0x0339,
	0x0360,
	0x0361,
	0x0368,
	0x0369,
	0x0370,
	0x0371,
	0x0378,
	0x0379,
	0x0222,
	0x0223,
	0x022a,
	0x022b,
	0x0232,
	0x0233,
	0x023a,
	0x023b,
	0x0262,
	0x0263,
	0x026a,
	0x026b,
	0x0272,
	0x0273,
	0x027a,
	0x027b,
	0x0322,
	0x0323,
	0x032a,
	0x032b,
	0x0332,
	0x0333,
	0x033a,
	0x033b,
	0x0362,
	0x0363,
	0x036a,
	0x036b,
	0x0372,
	0x0373,
	0x037a,
	0x037b,
	0x0224,
	0x0225,
	0x022c,
	0x022d,
	0x0234,
	0x0235,
	0x023c,
	0x023d,
	0x0264,
	0x0265,
	0x026c,
	0x026d,
	0x0274,
	0x0275,
	0x027c,
	0x027d,
	0x0324,
	0x0325,
	0x032c,
	0x032d,
	0x0334,
	0x0335,
	0x033c,
	0x033d,
	0x0364,
	0x0365,
	0x036c,
	0x036d,
	0x0374,
	0x0375,
	0x037c,
	0x037d,
	0x0226,
	0x0227,
	0x022e,
	0x022f,
	0x0236,
	0x0237,
	0x023e,
	0x023f,
	0x0266,
	0x0267,
	0x026e,
	0x026f,
	0x0276,
	0x0277,
	0x027e,
	0x027f,
	0x0326,
	0x0327,
	0x032e,
	0x032f,
	0x0336,
	0x0337,
	0x033e,
	0x033f,
	0x0366,
	0x0367,
	0x036e,
	0x036f,
	0x0376,
	0x0377,
	0x037e,
	0x037f,
	0x0280,
	0x0281,
	0x0288,
	0x0289,
	0x0290,
	0x0291,
	0x0298,
	0x0299,
	0x02c0,
	0x02c1,
	0x02c8,
	0x02c9,
	0x02d0,
	0x02d1,
	0x02d8,
	0x02d9,
	0x0380,
	0x0381,
	0x0388,
	0x0389,
	0x0390,
	0x0391,
	0x0398,
	0x0399,
	0x03c0,
	0x03c1,
	0x03c8,
	0x03c9,
	0x03d0,
	0x03d1,
	0x03d8,
	0x03d9,
	0x0282,
	0x0283,
	0x028a,
	0x028b,
	0x0292,
	0x0293,
	0x029a,
	0x029b,
	0x02c2,
	0x02c3,
	0x02ca,
	0x02cb,
	0x02d2,
	0x02d3,
	0x02da,
	0x02db,
	0x0382,
	0x0383,
	0x038a,
	0x038b,
	0x0392,
	0x0393,
	0x039a,
	0x039b,
	0x03c2,
	0x03c3,
	0x03ca,
	0x03cb,
	0x03d2,
	0x03d3,
	0x03da,
	0x03db,
	0x0284,
	0x0285,
	0x028c,
	0x028d,
	0x0294,
	0x0295,
	0x029c,
	0x029d,
	0x02c4,
	0x02c5,
	0x02cc,
	0x02cd,
	0x02d4,
	0x02d5,
	0x02dc,
	0x02dd,
	0x0384,
	0x0385,
	0x038c,
	0x038d,
	0x0394,
	0x0395,
	0x039c,
	0x039d,
	0x03c4,
	0x03c5,
	0x03cc,
	0x03cd,
	0x03d4,
	0x03d5,
	0x03dc,
	0x03dd,
	0x0286,
	0x0287,
	0x028e,
	0x028f,
	0x0296,
	0x0297,
	0x029e,
	0x029f,
	0x02c6,
	0x02c7,
	0x02ce,
	0x02cf,
	0x02d6,
	0x02d7,
	0x02de,
	0x02df,
	0x0386,
	0x0387,
	0x038e,
	0x038f,
	0x0396,
	0x0397,
	0x039e,
	0x039f,
	0x03c6,
	0x03c7,
	0x03ce,
	0x03cf,
	0x03d6,
	0x03d7,
	0x03de,
	0x03df,
	0x02a0,
	0x02a1,
	0x02a8,
	0x02a9,
	0x02b0,
	0x02b1,
	0x02b8,
	0x02b9,
	0x02e0,
	0x02e1,
	0x02e8,
	0x02e9,
	0x02f0,
	0x02f1,
	0x02f8,
	0x02f9,
	0x03a0,
	0x03a1,
	0x03a8,
	0x03a9,
	0x03b0,
	0x03b1,
	0x03b8,
	0x03b9,
	0x03e0,
	0x03e1,
	0x03e8,
	0x03e9,
	0x03f0,
	0x03f1,
	0x03f8,
	0x03f9,
	0x02a2,
	0x02a3,
	0x02aa,
	0x02ab,
	0x02b2,
	0x02b3,
	0x02ba,
	0x02bb,
	0x02e2,
	0x02e3,
	0x02ea,
	0x02eb,
	0x02f2,
	0x02f3,
	0x02fa,
	0x02fb,
	0x03a2,
	0x03a3,
	0x03aa,
	0x03ab,
	0x03b2,
	0x03b3,
	0x03ba,
	0x03bb,
	0x03e2,
	0x03e3,
	0x03ea,
	0x03eb,
	0x03f2,
	0x03f3,
	0x03fa,
	0x03fb,
	0x02a4,
	0x02a5,
	0x02ac,
	0x02ad,
	0x02b4,
	0x02b5,
	0x02bc,
	0x02bd,
	0x02e4,
	0x02e5,
	0x02ec,
	0x02ed,
	0x02f4,
	0x02f5,
	0x02fc,
	0x02fd,
	0x03a4,
	0x03a5,
	0x03ac,
	0x03ad,
	0x03b4,
	0x03b5,
	0x03bc,
	0x03bd,
	0x03e4,
	0x03e5,
	0x03ec,
	0x03ed,
	0x03f4,
	0x03f5,
	0x03fc,
	0x03fd,
	0x02a6,
	0x02a7,
	0x02ae,
	0x02af,
	0x02b6,
	0x02b7,
	0x02be,
	0x02bf,
	0x02e6,
	0x02e7,
	0x02ee,
	0x02ef,
	0x02f6,
	0x02f7,
	0x02fe,
	0x02ff,
	0x03a6,
	0x03a7,
	0x03ae,
	0x03af,
	0x03b6,
	0x03b7,
	0x03be,
	0x03bf,
	0x03e6,
	0x03e7,
	0x03ee,
	0x03ef,
	0x03f6,
	0x03f7,
	0x03fe,
	0x03ff,

};

static const __u16 spn_sbox[1024] = {
	0x0213,
	0x0210,
	0x0212,
	0x0211,
	0x021b,
	0x0218,
	0x021a,
	0x0219,
	0x0217,
	0x0214,
	0x0216,
	0x0215,
	0x021f,
	0x021c,
	0x021e,
	0x021d,
	0x0233,
	0x0230,
	0x0232,
	0x0231,
	0x023b,
	0x0238,
	0x023a,
	0x0239,
	0x0237,
	0x0234,
	0x0236,
	0x0235,
	0x023f,
	0x023c,
	0x023e,
	0x023d,
	0x0223,
	0x0220,
	0x0222,
	0x0221,
	0x022b,
	0x0228,
	0x022a,
	0x0229,
	0x0227,
	0x0224,
	0x0226,
	0x0225,
	0x022f,
	0x022c,
	0x022e,
	0x022d,
	0x0203,
	0x0200,
	0x0202,
	0x0201,
	0x020b,
	0x0208,
	0x020a,
	0x0209,
	0x0207,
	0x0204,
	0x0206,
	0x0205,
	0x020f,
	0x020c,
	0x020e,
	0x020d,
	0x0253,
	0x0250,
	0x0252,
	0x0251,
	0x025b,
	0x0258,
	0x025a,
	0x0259,
	0x0257,
	0x0254,
	0x0256,
	0x0255,
	0x025f,
	0x025c,
	0x025e,
	0x025d,
	0x0273,
	0x0270,
	0x0272,
	0x0271,
	0x027b,
	0x0278,
	0x027a,
	0x0279,
	0x0277,
	0x0274,
	0x0276,
	0x0275,
	0x027f,
	0x027c,
	0x027e,
	0x027d,
	0x0263,
	0x0260,
	0x0262,
	0x0261,
	0x026b,
	0x0268,
	0x026a,
	0x0269,
	0x0267,
	0x0264,
	0x0266,
	0x0265,
	0x026f,
	0x026c,
	0x026e,
	0x026d,
	0x0243,
	0x0240,
	0x0242,
	0x0241,
	0x024b,
	0x0248,
	0x024a,
	0x0249,
	0x0247,
	0x0244,
	0x0246,
	0x0245,
	0x024f,
	0x024c,
	0x024e,
	0x024d,
	0x02d3,
	0x02d0,
	0x02d2,
	0x02d1,
	0x02db,
	0x02d8,
	0x02da,
	0x02d9,
	0x02d7,
	0x02d4,
	0x02d6,
	0x02d5,
	0x02df,
	0x02dc,
	0x02de,
	0x02dd,
	0x02f3,
	0x02f0,
	0x02f2,
	0x02f1,
	0x02fb,
	0x02f8,
	0x02fa,
	0x02f9,
	0x02f7,
	0x02f4,
	0x02f6,
	0x02f5,
	0x02ff,
	0x02fc,
	0x02fe,
	0x02fd,
	0x02e3,
	0x02e0,
	0x02e2,
	0x02e1,
	0x02eb,
	0x02e8,
	0x02ea,
	0x02e9,
	0x02e7,
	0x02e4,
	0x02e6,
	0x02e5,
	0x02ef,
	0x02ec,
	0x02ee,
	0x02ed,
	0x02c3,
	0x02c0,
	0x02c2,
	0x02c1,
	0x02cb,
	0x02c8,
	0x02ca,
	0x02c9,
	0x02c7,
	0x02c4,
	0x02c6,
	0x02c5,
	0x02cf,
	0x02cc,
	0x02ce,
	0x02cd,
	0x0293,
	0x0290,
	0x0292,
	0x0291,
	0x029b,
	0x0298,
	0x029a,
	0x0299,
	0x0297,
	0x0294,
	0x0296,
	0x0295,
	0x029f,
	0x029c,
	0x029e,
	0x029d,
	0x02b3,
	0x02b0,
	0x02b2,
	0x02b1,
	0x02bb,
	0x02b8,
	0x02ba,
	0x02b9,
	0x02b7,
	0x02b4,
	0x02b6,
	0x02b5,
	0x02bf,
	0x02bc,
	0x02be,
	0x02bd,
	0x02a3,
	0x02a0,
	0x02a2,
	0x02a1,
	0x02ab,
	0x02a8,
	0x02aa,
	0x02a9,
	0x02a7,
	0x02a4,
	0x02a6,
	0x02a5,
	0x02af,
	0x02ac,
	0x02ae,
	0x02ad,
	0x0283,
	0x0280,
	0x0282,
	0x0281,
	0x028b,
	0x0288,
	0x028a,
	0x0289,
	0x0287,
	0x0284,
	0x0286,
	0x0285,
	0x028f,
	0x028c,
	0x028e,
	0x028d,
	0x0313,
	0x0310,
	0x0312,
	0x0311,
	0x031b,
	0x0318,
	0x031a,
	0x0319,
	0x0317,
	0x0314,
	0x0316,
	0x0315,
	0x031f,
	0x031c,
	0x031e,
	0x031d,
	0x0333,
	0x0330,
	0x0332,
	0x0331,
	0x033b,
	0x0338,
	0x033a,
	0x0339,
	0x0337,
	0x0334,
	0x0336,
	0x0335,
	0x033f,
	0x033c,
	0x033e,
	0x033d,
	0x0323,
	0x0320,
	0x0322,
	0x0321,
	0x032b,
	0x0328,
	0x032a,
	0x0329,
	0x0327,
	0x0324,
	0x0326,
	0x0325,
	0x032f,
	0x032c,
	0x032e,
	0x032d,
	0x0303,
	0x0300,
	0x0302,
	0x0301,
	0x030b,
	0x0308,
	0x030a,
	0x0309,
	0x0307,
	0x0304,
	0x0306,
	0x0305,
	0x030f,
	0x030c,
	0x030e,
	0x030d,
	0x0353,
	0x0350,
	0x0352,
	0x0351,
	0x035b,
	0x0358,
	0x035a,
	0x0359,
	0x0357,
	0x0354,
	0x0356,
	0x0355,
	0x035f,
	0x035c,
	0x035e,
	0x035d,
	0x0373,
	0x0370,
	0x0372,
	0x0371,
	0x037b,
	0x0378,
	0x037a,
	0x0379,
	0x0377,
	0x0374,
	0x0376,
	0x0375,
	0x037f,
	0x037c,
	0x037e,
	0x037d,
	0x0363,
	0x0360,
	0x0362,
	0x0361,
	0x036b,
	0x0368,
	0x036a,
	0x0369,
	0x0367,
	0x0364,
	0x0366,
	0x0365,
	0x036f,
	0x036c,
	0x036e,
	0x036d,
	0x0343,
	0x0340,
	0x0342,
	0x0341,
	0x034b,
	0x0348,
	0x034a,
	0x0349,
	0x0347,
	0x0344,
	0x0346,
	0x0345,
	0x034f,
	0x034c,
	0x034e,
	0x034d,
	0x03d3,
	0x03d0,
	0x03d2,
	0x03d1,
	0x03db,
	0x03d8,
	0x03da,
	0x03d9,
	0x03d7,
	0x03d4,
	0x03d6,
	0x03d5,
	0x03df,
	0x03dc,
	0x03de,
	0x03dd,
	0x03f3,
	0x03f0,
	0x03f2,
	0x03f1,
	0x03fb,
	0x03f8,
	0x03fa,
	0x03f9,
	0x03f7,
	0x03f4,
	0x03f6,
	0x03f5,
	0x03ff,
	0x03fc,
	0x03fe,
	0x03fd,
	0x03e3,
	0x03e0,
	0x03e2,
	0x03e1,
	0x03eb,
	0x03e8,
	0x03ea,
	0x03e9,
	0x03e7,
	0x03e4,
	0x03e6,
	0x03e5,
	0x03ef,
	0x03ec,
	0x03ee,
	0x03ed,
	0x03c3,
	0x03c0,
	0x03c2,
	0x03c1,
	0x03cb,
	0x03c8,
	0x03ca,
	0x03c9,
	0x03c7,
	0x03c4,
	0x03c6,
	0x03c5,
	0x03cf,
	0x03cc,
	0x03ce,
	0x03cd,
	0x0393,
	0x0390,
	0x0392,
	0x0391,
	0x039b,
	0x0398,
	0x039a,
	0x0399,
	0x0397,
	0x0394,
	0x0396,
	0x0395,
	0x039f,
	0x039c,
	0x039e,
	0x039d,
	0x03b3,
	0x03b0,
	0x03b2,
	0x03b1,
	0x03bb,
	0x03b8,
	0x03ba,
	0x03b9,
	0x03b7,
	0x03b4,
	0x03b6,
	0x03b5,
	0x03bf,
	0x03bc,
	0x03be,
	0x03bd,
	0x03a3,
	0x03a0,
	0x03a2,
	0x03a1,
	0x03ab,
	0x03a8,
	0x03aa,
	0x03a9,
	0x03a7,
	0x03a4,
	0x03a6,
	0x03a5,
	0x03af,
	0x03ac,
	0x03ae,
	0x03ad,
	0x0383,
	0x0380,
	0x0382,
	0x0381,
	0x038b,
	0x0388,
	0x038a,
	0x0389,
	0x0387,
	0x0384,
	0x0386,
	0x0385,
	0x038f,
	0x038c,
	0x038e,
	0x038d,
	0x0013,
	0x0010,
	0x0012,
	0x0011,
	0x001b,
	0x0018,
	0x001a,
	0x0019,
	0x0017,
	0x0014,
	0x0016,
	0x0015,
	0x001f,
	0x001c,
	0x001e,
	0x001d,
	0x0033,
	0x0030,
	0x0032,
	0x0031,
	0x003b,
	0x0038,
	0x003a,
	0x0039,
	0x0037,
	0x0034,
	0x0036,
	0x0035,
	0x003f,
	0x003c,
	0x003e,
	0x003d,
	0x0023,
	0x0020,
	0x0022,
	0x0021,
	0x002b,
	0x0028,
	0x002a,
	0x0029,
	0x0027,
	0x0024,
	0x0026,
	0x0025,
	0x002f,
	0x002c,
	0x002e,
	0x002d,
	0x0003,
	0x0000,
	0x0002,
	0x0001,
	0x000b,
	0x0008,
	0x000a,
	0x0009,
	0x0007,
	0x0004,
	0x0006,
	0x0005,
	0x000f,
	0x000c,
	0x000e,
	0x000d,
	0x0053,
	0x0050,
	0x0052,
	0x0051,
	0x005b,
	0x0058,
	0x005a,
	0x0059,
	0x0057,
	0x0054,
	0x0056,
	0x0055,
	0x005f,
	0x005c,
	0x005e,
	0x005d,
	0x0073,
	0x0070,
	0x0072,
	0x0071,
	0x007b,
	0x0078,
	0x007a,
	0x0079,
	0x0077,
	0x0074,
	0x0076,
	0x0075,
	0x007f,
	0x007c,
	0x007e,
	0x007d,
	0x0063,
	0x0060,
	0x0062,
	0x0061,
	0x006b,
	0x0068,
	0x006a,
	0x0069,
	0x0067,
	0x0064,
	0x0066,
	0x0065,
	0x006f,
	0x006c,
	0x006e,
	0x006d,
	0x0043,
	0x0040,
	0x0042,
	0x0041,
	0x004b,
	0x0048,
	0x004a,
	0x0049,
	0x0047,
	0x0044,
	0x0046,
	0x0045,
	0x004f,
	0x004c,
	0x004e,
	0x004d,
	0x00d3,
	0x00d0,
	0x00d2,
	0x00d1,
	0x00db,
	0x00d8,
	0x00da,
	0x00d9,
	0x00d7,
	0x00d4,
	0x00d6,
	0x00d5,
	0x00df,
	0x00dc,
	0x00de,
	0x00dd,
	0x00f3,
	0x00f0,
	0x00f2,
	0x00f1,
	0x00fb,
	0x00f8,
	0x00fa,
	0x00f9,
	0x00f7,
	0x00f4,
	0x00f6,
	0x00f5,
	0x00ff,
	0x00fc,
	0x00fe,
	0x00fd,
	0x00e3,
	0x00e0,
	0x00e2,
	0x00e1,
	0x00eb,
	0x00e8,
	0x00ea,
	0x00e9,
	0x00e7,
	0x00e4,
	0x00e6,
	0x00e5,
	0x00ef,
	0x00ec,
	0x00ee,
	0x00ed,
	0x00c3,
	0x00c0,
	0x00c2,
	0x00c1,
	0x00cb,
	0x00c8,
	0x00ca,
	0x00c9,
	0x00c7,
	0x00c4,
	0x00c6,
	0x00c5,
	0x00cf,
	0x00cc,
	0x00ce,
	0x00cd,
	0x0093,
	0x0090,
	0x0092,
	0x0091,
	0x009b,
	0x0098,
	0x009a,
	0x0099,
	0x0097,
	0x0094,
	0x0096,
	0x0095,
	0x009f,
	0x009c,
	0x009e,
	0x009d,
	0x00b3,
	0x00b0,
	0x00b2,
	0x00b1,
	0x00bb,
	0x00b8,
	0x00ba,
	0x00b9,
	0x00b7,
	0x00b4,
	0x00b6,
	0x00b5,
	0x00bf,
	0x00bc,
	0x00be,
	0x00bd,
	0x00a3,
	0x00a0,
	0x00a2,
	0x00a1,
	0x00ab,
	0x00a8,
	0x00aa,
	0x00a9,
	0x00a7,
	0x00a4,
	0x00a6,
	0x00a5,
	0x00af,
	0x00ac,
	0x00ae,
	0x00ad,
	0x0083,
	0x0080,
	0x0082,
	0x0081,
	0x008b,
	0x0088,
	0x008a,
	0x0089,
	0x0087,
	0x0084,
	0x0086,
	0x0085,
	0x008f,
	0x008c,
	0x008e,
	0x008d,
	0x0113,
	0x0110,
	0x0112,
	0x0111,
	0x011b,
	0x0118,
	0x011a,
	0x0119,
	0x0117,
	0x0114,
	0x0116,
	0x0115,
	0x011f,
	0x011c,
	0x011e,
	0x011d,
	0x0133,
	0x0130,
	0x0132,
	0x0131,
	0x013b,
	0x0138,
	0x013a,
	0x0139,
	0x0137,
	0x0134,
	0x0136,
	0x0135,
	0x013f,
	0x013c,
	0x013e,
	0x013d,
	0x0123,
	0x0120,
	0x0122,
	0x0121,
	0x012b,
	0x0128,
	0x012a,
	0x0129,
	0x0127,
	0x0124,
	0x0126,
	0x0125,
	0x012f,
	0x012c,
	0x012e,
	0x012d,
	0x0103,
	0x0100,
	0x0102,
	0x0101,
	0x010b,
	0x0108,
	0x010a,
	0x0109,
	0x0107,
	0x0104,
	0x0106,
	0x0105,
	0x010f,
	0x010c,
	0x010e,
	0x010d,
	0x0153,
	0x0150,
	0x0152,
	0x0151,
	0x015b,
	0x0158,
	0x015a,
	0x0159,
	0x0157,
	0x0154,
	0x0156,
	0x0155,
	0x015f,
	0x015c,
	0x015e,
	0x015d,
	0x0173,
	0x0170,
	0x0172,
	0x0171,
	0x017b,
	0x0178,
	0x017a,
	0x0179,
	0x0177,
	0x0174,
	0x0176,
	0x0175,
	0x017f,
	0x017c,
	0x017e,
	0x017d,
	0x0163,
	0x0160,
	0x0162,
	0x0161,
	0x016b,
	0x0168,
	0x016a,
	0x0169,
	0x0167,
	0x0164,
	0x0166,
	0x0165,
	0x016f,
	0x016c,
	0x016e,
	0x016d,
	0x0143,
	0x0140,
	0x0142,
	0x0141,
	0x014b,
	0x0148,
	0x014a,
	0x0149,
	0x0147,
	0x0144,
	0x0146,
	0x0145,
	0x014f,
	0x014c,
	0x014e,
	0x014d,
	0x01d3,
	0x01d0,
	0x01d2,
	0x01d1,
	0x01db,
	0x01d8,
	0x01da,
	0x01d9,
	0x01d7,
	0x01d4,
	0x01d6,
	0x01d5,
	0x01df,
	0x01dc,
	0x01de,
	0x01dd,
	0x01f3,
	0x01f0,
	0x01f2,
	0x01f1,
	0x01fb,
	0x01f8,
	0x01fa,
	0x01f9,
	0x01f7,
	0x01f4,
	0x01f6,
	0x01f5,
	0x01ff,
	0x01fc,
	0x01fe,
	0x01fd,
	0x01e3,
	0x01e0,
	0x01e2,
	0x01e1,
	0x01eb,
	0x01e8,
	0x01ea,
	0x01e9,
	0x01e7,
	0x01e4,
	0x01e6,
	0x01e5,
	0x01ef,
	0x01ec,
	0x01ee,
	0x01ed,
	0x01c3,
	0x01c0,
	0x01c2,
	0x01c1,
	0x01cb,
	0x01c8,
	0x01ca,
	0x01c9,
	0x01c7,
	0x01c4,
	0x01c6,
	0x01c5,
	0x01cf,
	0x01cc,
	0x01ce,
	0x01cd,
	0x0193,
	0x0190,
	0x0192,
	0x0191,
	0x019b,
	0x0198,
	0x019a,
	0x0199,
	0x0197,
	0x0194,
	0x0196,
	0x0195,
	0x019f,
	0x019c,
	0x019e,
	0x019d,
	0x01b3,
	0x01b0,
	0x01b2,
	0x01b1,
	0x01bb,
	0x01b8,
	0x01ba,
	0x01b9,
	0x01b7,
	0x01b4,
	0x01b6,
	0x01b5,
	0x01bf,
	0x01bc,
	0x01be,
	0x01bd,
	0x01a3,
	0x01a0,
	0x01a2,
	0x01a1,
	0x01ab,
	0x01a8,
	0x01aa,
	0x01a9,
	0x01a7,
	0x01a4,
	0x01a6,
	0x01a5,
	0x01af,
	0x01ac,
	0x01ae,
	0x01ad,
	0x0183,
	0x0180,
	0x0182,
	0x0181,
	0x018b,
	0x0188,
	0x018a,
	0x0189,
	0x0187,
	0x0184,
	0x0186,
	0x0185,
	0x018f,
	0x018c,
	0x018e,
	0x018d,

};

static inline __u16 spn_sub(__u16 x)
{
	return spn_sbox[x];
}
static inline __u16 spn_perm(__u16 x)
{
	return spn_pbox[x];
}

static __u16 spn(const __u16 m, const __u16 k[16])
{
	__u16 ans = m ^ k[0];
	int i;
	for (i = 1; i < 15; i++)
	{
		spn_sub(ans);
		spn_perm(ans);
		ans ^= k[i];
	}
	spn_sub(ans);
	ans ^= k[15];
	return ans;
}

int transposition_cipher(struct myfs_key fkey, unsigned long i_no, unsigned long long block_no, __u16 lentry_no)
{
	__u32 rno = (i_no * block_no + i_no << 1 + block_no) / (block_no % (i_no % 20) + 1);
	__u8 key_block[MYFS_KEY_LEN + 4];
	__u16 rkeys[16];
	int i;
	key_block[0] = rno >> 16;
	key_block[1] = rno >> 24;
	key_block[MYFS_KEY_LEN] = rno;
	key_block[MYFS_KEY_LEN + 1] = rno >> 8;
	memcpy(key_block + 2, fkey.key, MYFS_KEY_LEN);
	for (i = 0; i < 16; i++)
	{
		switch ((i * 10) % 8)
		{
		case 0:
			rkeys[i] = ((__u16)key_block[(i * 10) / 8] & 0xff) << 2;
			rkeys[i] |= ((__u16)key_block[(i * 10) / 8 + 1] & 0xc0) >> 6;
			break;
		case 2:
			rkeys[i] = ((__u16)key_block[(i * 10) / 8] & 0x3f) << 4;
			rkeys[i] |= ((__u16)key_block[(i * 10) / 8 + 1] & 0xf0) >> 4;
			break;

		case 4:
			rkeys[i] = ((__u16)key_block[(i * 10) / 8] & 0x0f) << 6;
			rkeys[i] |= ((__u16)key_block[(i * 10) / 8 + 1] & 0xfc) >> 2;
			break;
		case 6:
			rkeys[i] = ((__u16)key_block[(i * 10) / 8] & 0x03) << 8;
			rkeys[i] |= ((__u16)key_block[(i * 10) / 8 + 1] & 0xff) >> 0;
			break;
		}
	}
	return spn(lentry_no, rkeys);
}

void combine_keys(const struct myfs_key *to_add, struct myfs_key *add_to)
{
	int i;
	for (i = 0; i < MYFS_KEY_LEN; i++)
		add_to->key[i] ^= to_add->key[i];
}