// Copyright 2018 Schibsted

#include <assert.h>
#include <string.h>
#include "rcycle.h"
#include <stdio.h>

static void lfsr_init(struct rcycle_lfsr *lfsr, unsigned int bits, uint32_t seed);


static const uint32_t lfsr_taps[33] = {
	0,		/* bits range taps */
	0,		/* Verboten. */
	0x3,		/* 2	3*		[0,1] 		*/
	0x5,		/* 3	7*		[0,2] 		*/
	0x9,		/* 4	15		[0,3] 		*/
	0x12,		/* 5	31*		[1,4] 		*/
	0x21,		/* 6	63		[0,5] 		*/
	0x41,		/* 7	127*		[0,6] 		*/
	0x8e,		/* 8	255		[1,2,3,7]	*/
	0x108,		/* 9	511		[3,8]		*/
	0x204,		/* 10	1023		[2,9]		*/
	0x402,		/* 11	2047		[1,10]		*/
	0x829,		/* 12	4095		[0,3,5,11]	*/
	0x100d,		/* 13	8191*		[0,2,3,12]	*/
	0x2015,		/* 14	16383		[0,2,4,13]	*/
	0x4001,		/* 15	32767		[0,14]		*/
	0x8016,		/* 16	65535		[1,2,4,15]	*/
	0x10004,	/* 17	131071*		[2,16]		*/
	0x20040,	/* 18	262143		[6,17]		*/
	0x40013,	/* 19	524287*		[0,1,4,18]	*/
	0x80004,	/* 20	1048575		[2,19]		*/
	0x100002,	/* 21	2097151		[1,20]		*/
	0x200001,	/* 22	4194303		[0,21]		*/
	0x400010,	/* 23	8388607		[4,22]		*/
	0x80000d,	/* 24	16777215	[0,2,3,23]	*/
	0x1000080,	/* 25	33554431	[7,24]		*/
	0x2000023,	/* 26	67108863	[0,1,5,25]	*/
	0x4000013,	/* 27	134217727	[0,1,4,26]	*/
	0x8000004,	/* 28	268435455	[2,27]		*/
	0x10000002,	/* 29	536870911	[1,28]		*/
	0x20000029,	/* 30	1073741823	[0,3,5,29]	*/
	0x40000004,	/* 31	2147483647*	[2,30]		*/
	0x80000062,	/* 32	4294967295	[1,5,6,31]	*/
};

static void
lfsr_init(struct rcycle_lfsr *lfsr, unsigned int bits, uint32_t seed) {
	assert(bits >= 2 && bits <= 32);
	lfsr->lfsr_bits = bits;
	lfsr->lfsr_taps = lfsr_taps[bits];
	lfsr->lfsr_state = seed & (bits == 32 ? (uint32_t)0xffffffff : (uint32_t)((1 << bits) - 1));
}


static uint32_t
lfsr_generate_u32(struct rcycle_lfsr *lfsr) {
	uint32_t ret = lfsr->lfsr_state;
	lfsr->lfsr_state = (lfsr->lfsr_state >> 1) ^ (-(lfsr->lfsr_state & 1) & lfsr->lfsr_taps);
	return (ret);
}

static inline uint32_t
round_2(uint32_t x) {
	unsigned int i;
	for (i = 1; i < sizeof(x) * 8; i <<= 1)
		x |= x >> i;
	return (x + 1);
}

static void
rcycle_reseed(struct rcycle *rc, uint32_t seed) {
	unsigned int bits = ffs(round_2(rc->rc_range + 1)) - 1;
	uint32_t mask = (1 << bits) - 1;

	if (bits < 16) {
		rc->rc_flip = seed >> (bits + 1);
	} else {
		rc->rc_flip = seed >> (32 - bits);
	}

	/*
	 * All this flipping and fiddling with the seed
	 * is here so that we get a good distribution
	 * on the starting points and so that we don't
	 * bias too hard toward a certain sequences.
	 * All this is empirical and hand-fiddled so that
	 * all the numbers in the sequences are equally
	 * likely in every position in the stream and
	 * so that every possible stream is equally
	 * likely.
	 */
	rc->rc_flip &= mask;
	if (rc->rc_flip < rc->rc_range)
		rc->rc_flip ^= mask;

	seed = (seed % rc->rc_range) + 1;
	if ((seed ^ rc->rc_flip) > rc->rc_range)
		seed ^= rc->rc_flip;
		
	lfsr_init(&rc->rc_lfsr, bits, seed);
}

void
rcycle_init(struct rcycle *rc, unsigned int range, uint32_t seed) {
	rc->rc_range = range;
	rc->rc_count = 0;
	lfsr_init(&rc->rc_lfsr_reseed, 32, seed);
	rcycle_reseed(rc, seed);
}

uint32_t
rcycle_generate(struct rcycle *rc) {
	uint32_t ret;

	do {
		ret = lfsr_generate_u32(&rc->rc_lfsr) ^ rc->rc_flip;
		if (ret == 0)
			ret = rc->rc_flip;
	} while (ret > rc->rc_range);

	if ((++rc->rc_count % rc->rc_range) == 0)
		rcycle_reseed(rc, lfsr_generate_u32(&rc->rc_lfsr_reseed));

	return (ret - 1);
}

#ifdef TEST

int
main(int argc, char **argv)
{
	int range = argc > 1 ? atoi(argv[1]) : 4;
	int tested[range];
	struct rcycle r;
	int i, j, k;
	int rounds = argc > 2 ? atoi(argv[2]) : range * 10;
	int bigrounds = argc > 3 ? atoi(argv[3]) : 1;

	srandom(time(NULL) ^ getpid());

	for (k = 0 ; k < bigrounds ; k++) {
		rcycle_init(&r, range, random()/*0x47114711*/);

		for (j = 0; j < rounds; j++) {
			memset(tested, 0, sizeof(tested));

			for (i = 0; i < range; i++) {
				int val = rcycle_generate(&r);

				if (tested[val])
					fprintf(stderr, "perm %d val %d %s\n", i, val, tested[val] ? "(dup)" : "");
				tested[val] = 1;
				printf("%x ", val);
			}
			printf("\n");
		}
	}

	return 0;
}
#endif
