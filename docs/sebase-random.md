# Proper random library #

This library provides proper randomness primitives to generate
cryptographically secure random numbers trivially callable from any
context.

## API ##

Just like OpenBSD (and LibreSSL) the only interface provided is:

    uint32_t arc4random(void);

Returns one random number in [0,2^32).

    uint32_t arc4random_uniform(uint32_t max);

Returns on random number in [0,max)

    void arc4random_buf(void *buf, size_t sz);

Fills the buffer `buf` with `sz` bytes of random data.

We also provide this function from OpenBSD as a convenience:

    void explicit_bzero(void *buf, size_t len);

This is equivalent to `memset(buf, 0, len);` but written in such a way
to make all effort to prevent compilers from optimizing it away as a
dead store.

There is no initialization, reinitialization and seeding required. The
functions are thread safe and callable from almost any context.

## Imported from ##

It contains the following files from OpenBSD:

lib/libc/string/explicit_bzero.c
lib/libc/crypt/chacha_private.h
lib/libc/crypt/arc4random.c
lib/libc/crypt/arc4random_uniform.c
lib/libcrypto/crypto/getentropy_linux.c
lib/libcrypto/crypto/arc4random_linux.h -> arc4random.h

The code (at this moment) is from OpenBSD cvs from 2015-08-13. It has
been imported without modifications, then modified in this tree to
make it work with our build system and Linux to make the changes easier
to track.

Currently the only modification necessary was to write rand.h and include
it all over the place.

## COPYRIGHT

Copyright 2018 Schibsted
