// Copyright 2018 Schibsted

/*
	Date functions
*/
#ifndef PLATFORM_DATE_FUNCTIONS_H
#define PLATFORM_DATE_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "macros.h"

struct tm;
struct date_rec {
	int year;
	int month;
	int day;
	int day_of_year;
};

void date_set_prev_day(struct date_rec *d) NONNULL_ALL;
void date_set_next_day(struct date_rec *d) NONNULL_ALL;
void date_set_day_offset(struct date_rec *d, int offset) NONNULL_ALL;
int date_days_in_month(struct date_rec *d) NONNULL_ALL;
int date_cmp(struct date_rec *lhs, struct date_rec *rhs) NONNULL_ALL FUNCTION_PURE;
void date_set(struct date_rec *date, struct tm *t) NONNULL_ALL;

size_t
date_format_rfc1123(char *buf, size_t len, time_t t);

#ifdef __cplusplus
}
#endif

#endif

