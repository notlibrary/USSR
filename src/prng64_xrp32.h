/* 
The XRP PRNG Xor Rotate Pair Pseudo Random Number Generator
 * 0-CLAUSE BSD LICENSE.
 */


#ifndef RNG64_XRP32_H
#define RNG64_XRP32_H
#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

uint64_t prng64_xrp32(void);
void seed_xrp32(uint64_t seed);
#define XRP32_TABLE_ID xrp->table
#define TABLE_SIZE_BYTES 256
/* The table must be seeded with DISTINCT 0-255 chars in random order. */
#define SHIFTED_WORD_WIDTH 64
#define XRP_MAX ULONG_MAX
#define BYTES_IN_WORD 8
#define WORDS_IN_TABLE 32
#ifdef PAIR_STREAM_CIPHER
typedef struct 
{
	uint32_t keystream32[16];
	size_t position;

	uint8_t key[32];
	uint8_t nonce[12];
	uint64_t counter;

	uint32_t state[16];
}chacha20_context_t;
#endif
typedef struct 
{
#ifdef PAIR_TOY_TEST	
	unsigned char table[TABLE_SIZE_BYTES];
#endif
#ifdef PAIR_STREAM_CIPHER
	chacha20_context_t ctx;
#endif
	uint64_t w;
	uint64_t x;
	uint64_t y;
	uint64_t z;
	uint64_t counter;	
}xrp_state_t;
typedef struct  
{
	uint64_t s;
}splitmix64_state_t;


#if !defined (PAIR_TOY_TEST) && !defined (PAIR_CRYPTO_HASH) && !defined (PAIR_STREAM_CIPHER)
#define PAIR_NULL_RAW
#endif
#ifdef __cplusplus
}
#endif //__cpluplus
#endif //RNG64_XRP32_H
