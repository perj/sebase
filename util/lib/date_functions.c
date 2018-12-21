// Copyright 2018 Schibsted

#include <time.h>
#include <stdlib.h>

#include "date_functions.h"

void
date_set(struct date_rec *date, struct tm *t) {
	date->year = t->tm_year + 1900;
	date->month = t->tm_mon + 1;
	date->day = t->tm_mday;
	date->day_of_year = t->tm_yday + 1;
}

int
date_cmp(struct date_rec *lhs, struct date_rec *rhs) {
	if (lhs->year != rhs->year)
		return (rhs->year - lhs->year);

	if (lhs->month != rhs->month)
		return (rhs->month - lhs->month);

	return (rhs->day - lhs->day);
}

int
date_days_in_month(struct date_rec *d) {
	switch (d->month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		return 31;
	case 4:
	case 6:
	case 9:
	case 11:
		return 30;
	case 2:
		return (d->year % 4 != 0 || (d->year % 100 == 0 && d->year % 400 != 0)) ? 28 : 29;
	}

	return 0;
}

void
date_set_prev_day(struct date_rec *d) {
	d->day_of_year--;

	if (d->day > 1) {
		d->day--;
	} else if (d->month > 1) {
		d->month--;
		d->day = date_days_in_month(d);
	} else {
		d->year--;
		d->month = 12;
		d->day = 31;
		d->day_of_year = (d->year % 4 != 0 || (d->year % 100 == 0 && d->year % 400 != 0)) ? 365 : 366;
	}

}

void
date_set_next_day(struct date_rec *d) {
	d->day_of_year++;

	if (d->day == date_days_in_month(d)) {
		if (d->month == 12) {
			d->year++;
			d->month = 1;
			d->day_of_year = 1;
		} else {
			d->month++;
		}

		d->day = 1;
	} else {
		d->day++;
	}
}

void
date_set_day_offset(struct date_rec *d, int offset) {
	int inc = (offset > 0);
	int i;

	offset = abs(offset);

	for (i = 0; i < offset; i++) {
		if (inc)
			date_set_next_day(d);
		else
			date_set_prev_day(d);
	}
}

size_t
date_format_rfc1123(char *buf, size_t len, time_t t) {
	struct tm gmt;
	gmtime_r(&t, &gmt);
	return strftime(buf, len, "%a, %d %b %Y %H:%M:%S GMT", &gmt);
}
