/*
 * Copyright (c) 2003 Artur Grabowski <art@openbsd.org>
 * Copyright 2018 Schibsted
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#if defined(__ELF__) || defined(__APPLE__)

#ifdef __APPLE__
#ifndef __LP64__
#error "OS X code is only implemented for 64 bit architecture."
#endif
#include <mach-o/getsect.h>
#include <mach-o/dyld.h>
#define LINKER_SET_DECLARE(set, ptype)					\
	ptype **__start_link_set_##set;					\
	ptype **__stop_link_set_##set;					\
	do {								\
		const struct section_64 *__temp_##set;			\
		int _idx = 0;						\
		const struct mach_header_64 *hdr;				\
		while ((hdr = (const struct mach_header_64*)_dyld_get_image_header(_idx)) &&	\
		      !(__temp_##set = getsectbynamefromheader_64(hdr, 	\
			      "__DATA",					\
			      "lset_" #set))) _idx++;		\
		__start_link_set_##set = 				\
		__temp_##set ? (ptype **)(__temp_##set->addr + 			\
			   _dyld_get_image_vmaddr_slide(_idx)) : NULL;		\
		__stop_link_set_##set = __temp_##set ? (ptype **)(__temp_##set->addr +	\
		    __temp_##set->size +				\
		    _dyld_get_image_vmaddr_slide(_idx)) : NULL;			\
	} while (0)
	
#define LINKER_SET_START(set) (__start_link_set_##set)
#define LINKER_SET_END(set) (__stop_link_set_##set)

#define LINKER_SET_ENTRY(set, sym)					\
	static void const * const __link_set_##set##_sym_##sym		\
	__attribute__((__section__("__DATA, lset_" #set)))	\
	__attribute__((__unused__)) 					\
	__attribute__((__used__)) = &sym

#else

#ifdef __OpenBSD__
#define LINKER_SET_DECLARE(set, ptype)					\
	extern ptype *__start_link_set_##set __attribute__((weak, visibility ("default"))); \
	extern ptype *__stop_link_set_##set __attribute__((weak, visibility ("default")))
#else
#define LINKER_SET_DECLARE(set, ptype)					\
	extern ptype *__start_link_set_##set __attribute__((weak, visibility ("hidden"))); \
	extern ptype *__stop_link_set_##set __attribute__((weak, visibility ("hidden")))
#endif

#define LINKER_SET_START(set) (&__start_link_set_##set)
#define LINKER_SET_END(set) (&__stop_link_set_##set)

#define LINKER_SET_ENTRY(set, sym)					\
	static void const * const __link_set_##set##_sym_##sym		\
	__attribute__((__section__("link_set_" #set)))	\
	__attribute__((__unused__)) 					\
	__attribute__((__used__)) = &sym
#endif

#define LINKER_SET_ADD_TEXT(set, sym) LINKER_SET_ENTRY(set, sym)
#define LINKER_SET_ADD_RODATA(set, sym) LINKER_SET_ENTRY(set, sym)
#define LINKER_SET_ADD_DATA(set, sym) LINKER_SET_ENTRY(set, sym)
#define LINKER_SET_ADD_BSS(set, sym) LINKER_SET_ENTRY(set, sym)

#else

#define LINKER_SET_DECLARE(set, ptype)					\
extern struct {								\
	int	__ls_length;						\
	ptype	*__ls_items[1];						\
} __link_set_##set

#define LINKER_SET_ENTRY(set, sym, type)				\
	static void const * const __link_set_##set##_sym_##sym		\
	__attribute__((__used__))					\
	__attribute__((__unused__)) = &sym;				\
	__asm(".stabs \"___link_set_" #set "\", " #type ", 0, 0, _" #sym)

#define LINKER_SET_START(set) (&(__link_set_##set).__ls_items[0])
#define LINKER_SET_END(set) 						\
	(&(__link_set_##set).__ls_items[(__link_set_##set).__ls_length])

#define LINKER_SET_ADD_TEXT(set, sym) LINKER_SET_ENTRY(set, sym, 23)
#define LINKER_SET_ADD_RODATA(set, sym) LINKER_SET_ENTRY(set, sym, 23)
#define LINKER_SET_ADD_DATA(set, sym) LINKER_SET_ENTRY(set, sym, 25)
#define LINKER_SET_ADD_BSS(set, sym) LINKER_SET_ENTRY(set, sym, 27)

#endif

#define LINKER_SET_FOREACH(_v, set)					\
	for (_v = LINKER_SET_START(set); _v && _v < LINKER_SET_END(set); _v++)
