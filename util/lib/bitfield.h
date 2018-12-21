// Copyright 2018 Schibsted

#ifndef BITFIELD_H
#define BITFIELD_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Multiple of 64 assumed. */
#define BITFIELD(n, bits) \
	static_assert((bits) % 64 == 0 && (bits) >= 128, "BITFIELD must be multiple of 64 and at least 128"); \
	uint64_t (n)[(bits) / (sizeof(uint64_t) * CHAR_BIT)]
#define BITFIELD_SZ(n) (sizeof(n) * CHAR_BIT)

static inline void
_bitfield_set(uint64_t *bf, int bfsz, int bit) {
	assert(bit < bfsz);

	bf[bit / (sizeof(*bf) * CHAR_BIT)] |= 1 << (bit % (sizeof (*bf) * CHAR_BIT));
}
#define bitfield_set(bf, bit) _bitfield_set(bf, BITFIELD_SZ(bf), (bit))

static inline void
_bitfield_clear(uint64_t *bf, int bfsz, int bit) {
	assert(bit < bfsz);

	bf[bit / (sizeof(*bf) * CHAR_BIT)] &= ~(1 << (bit % (sizeof (*bf) * CHAR_BIT)));
}
#define bitfield_clear(bf, bit) _bitfield_clear(bf, BITFIELD_SZ(bf), (bit))

static inline uint64_t
_bitfield_isset(uint64_t *bf, int bfsz, int bit) {
	assert(bit < bfsz);

	return bf[bit / (sizeof(*bf) * CHAR_BIT)] & (1 << (bit % (sizeof (*bf) * CHAR_BIT)));
}
#define bitfield_isset(bf, bit) _bitfield_isset(bf, BITFIELD_SZ(bf), (bit))

static inline void
_bitfield_setall(uint64_t *bf, int bfsz) {
	uint64_t *end = bf + bfsz / (sizeof(*bf) * CHAR_BIT);

	memset(bf, 0xFF, (char*)end - (char*)bf);
}
#define bitfield_setall(bf) _bitfield_setall(bf, BITFIELD_SZ(bf))

static inline bool
_bitfield_iszero(uint64_t *bf, int bfsz) {
	uint64_t *end = bf + bfsz / (sizeof(*bf) * CHAR_BIT);

	for ( ; bf < end ; bf++) {
		if (*bf)
			return false;
	}
	return true;
}
#define bitfield_iszero(bf) _bitfield_iszero(bf, BITFIELD_SZ(bf))

static inline int
_bitfield_ffs(uint64_t *bf, int bfsz) {
	uint64_t *end = bf + bfsz / (sizeof(*bf) * CHAR_BIT);
	int v;
	int i = 0;

	while (bf < end) {
		if ((v = ffsll(*bf)))
			return i + v;
		i += sizeof(*bf) * CHAR_BIT;
		bf++;
	}
	return 0;
}
#define bitfield_ffs(bf) _bitfield_ffs(bf, BITFIELD_SZ(bf))

/* Add operations as needed. Don't forget to send an MR. */

static inline uint64_t
_bitfield_union3_isset(uint64_t *bfa, uint64_t *bfb, uint64_t *bfc, int bfsz, int bit) {
	assert(bit < bfsz);
	int idx = bit / (sizeof(*bfa) * CHAR_BIT);
	uint64_t mask = 1ULL << (bit % (sizeof(*bfa) * CHAR_BIT));

	return (bfa[idx] | bfb[idx] | bfc[idx]) & mask;
}
#define bitfield_union3_isset(bfa, bfb, bfc, bit) ({ \
	static_assert(BITFIELD_SZ(bfa) == BITFIELD_SZ(bfb), "bitfield size mismatch"); \
	static_assert(BITFIELD_SZ(bfa) == BITFIELD_SZ(bfc), "bitfield size mismatch"); \
	static_assert(sizeof(bfa) > sizeof(void*), "bitfield might be pointer (use _ prefixed version to avoid this error)"); \
	_bitfield_union3_isset(bfa, bfb, bfc, BITFIELD_SZ(bfa), (bit)); })

static inline bool
_bitfield_union2_check_complement_all_set(uint64_t *ubfa, uint64_t *ubfb, uint64_t *cbf, int bfsz) {
	uint64_t *end = ubfa + bfsz / (sizeof(*ubfa) * CHAR_BIT);

	while (ubfa < end) {
		if (((*ubfa | *ubfb) & ~*cbf) != ~*cbf)
			return false;
		ubfa++;
		ubfb++;
		cbf++;
	}
	return true;
}
#define bitfield_union2_check_complement_all_set(ubfa, ubfb, cbf) ({ \
	static_assert(BITFIELD_SZ(ubfa) == BITFIELD_SZ(ubfb), "bitfield size mismatch"); \
	static_assert(BITFIELD_SZ(ubfa) == BITFIELD_SZ(cbf), "bitfield size mismatch"); \
	static_assert(sizeof(ubfa) > sizeof(void*), "bitfield might be pointer (use _ prefixed version to avoid this error)"); \
	_bitfield_union2_check_complement_all_set(ubfa, ubfb, cbf, BITFIELD_SZ(ubfa)); })

static inline bool
_bitfield_union3_check_complement_all_set(uint64_t *ubfa, uint64_t *ubfb, uint64_t *ubfc, uint64_t *cbf, int bfsz) {
	uint64_t *end = ubfa + bfsz / (sizeof(*ubfa) * CHAR_BIT);

	while (ubfa < end) {
		if (((*ubfa | *ubfb | *ubfc) & ~*cbf) != ~*cbf)
			return false;
		ubfa++;
		ubfb++;
		ubfc++;
		cbf++;
	}
	return true;
}
#define bitfield_union3_check_complement_all_set(ubfa, ubfb, ubfc, cbf) ({ \
	static_assert(BITFIELD_SZ(ubfa) == BITFIELD_SZ(ubfb), "bitfield size mismatch"); \
	static_assert(BITFIELD_SZ(ubfa) == BITFIELD_SZ(ubfc), "bitfield size mismatch"); \
	static_assert(BITFIELD_SZ(ubfa) == BITFIELD_SZ(cbf), "bitfield size mismatch"); \
	static_assert(sizeof(ubfa) > sizeof(void*), "bitfield might be pointer (use _ prefixed version to avoid this error)"); \
	_bitfield_union3_check_complement_all_set(ubfa, ubfb, ubfc, cbf, BITFIELD_SZ(ubfa)); })

static inline bool
_bitfield_compare_equal(uint64_t *a, uint64_t *b, int bfsz) {
	return !memcmp(a, b, bfsz / CHAR_BIT);
}
#define bitfield_compare_equal(a, b) ({ \
	static_assert(BITFIELD_SZ(a) == BITFIELD_SZ(b), "bitfield size mismatch"); \
	static_assert(sizeof(a) > sizeof(void*), "bitfield might be pointer (use _ prefixed version to avoid this error)"); \
	_bitfield_compare_equal(a, b, BITFIELD_SZ(a)); })

#endif /*BITFIELD_H*/
