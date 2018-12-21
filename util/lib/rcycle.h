// Copyright 2018 Schibsted

/*
 * "Random" cycle generator.
 *
 * Given a random seed and a cycle length, generates a cycle of integeres
 * returning every integer in the range. The cycle is not very unpredictable,
 * don't count on it being that, it's just that we get different cycles
 * for different seeds.
 *
 * Uses a linear feedback shift register to generate the sequences.
 * http://en.wikipedia.org/wiki/Linear_feedback_shift_register
 *
 * The polynomials are picked from tables by rounding the range to the nearest
 * 2^n-1 and then discarding every value that's outside the range, it means
 * that at most we'll discard half the values from the generator.
 *
 * The maximal range is (2^32)-1. It can easily be extended to (2^64)-1, but
 * don't forget to fill the tables.
 *
 * The lfsr will repeat itself after 'range' elements have been extracted from
 * it. For that we set up another lfsr with a 32 bit cycle to reseed the main
 * lfsr after each cycle. Therefore it is important that the seed is full 32
 * bits (since the same seed is used for both generators). 
 *
 * To not get the same sequences with just a different starting point, we xor the
 * values from the lfsr with part of the seed.
 *
 * To repeat: This is not about unpredictability or entropy. We get reproducability
 * with the same seed and a cover the whole spectrum.
 */

#ifndef RCYCLE_H
#define RCYCLE_H

#include <stdint.h>

struct rcycle_lfsr {
	uint32_t lfsr_state;
	unsigned int lfsr_bits;
	uint32_t lfsr_taps;
};

struct rcycle {
	struct rcycle_lfsr rc_lfsr;
	struct rcycle_lfsr rc_lfsr_reseed;
	unsigned int rc_range;
	unsigned int rc_count;
	uint32_t rc_flip;
};

void rcycle_init(struct rcycle *rc, unsigned int range, uint32_t seed);
uint32_t rcycle_generate(struct rcycle *rc);

#endif
