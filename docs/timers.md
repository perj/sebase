# Timers

Copyright 2019 Schibsted

This utility interface is a set of timers primarily meant for performance
testing. You can use this as an alternative to profiling code. The code is low
overhead and shouldn't affect the times recorded much, so it should be fine to
include them in production code normal usage. There is however some overhead so
they're not appropriate for micro-benchmarks.

The timers measure the total time spent between `timer_start` and the matching
`timer_end`, summing it up for all calls to this code part. It also counts the
number of entries to calculate the average, and keeps track of the min and max
values.

## Sub timers

The `timer_start` function can be called with an existing timer to start a sub
timer. Sub timers must be ended before the parent ones. Sub timers are reported
separately from the parent timer in a structural manner. It often makes sense to
use `timer_handover` to change which key is used for a sub timer, when switching
the part to be recorded.

## Attributes

Different code parts can be timed by adding different attributes. The timer is
then split on the different attributes which are reported separately, as well
as the total. This is good for separating error paths for example, since they
might be much faster or much slower.

## Fetching the data

You have to fetch the reports yourself using the `timer_foraech` function, then
print it in a good fashion. If you're using the C controllers library there is
however a default controller that reports timers for you, on URL path `/stats`.
