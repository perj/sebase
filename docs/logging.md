# Logging

Copyright 2019 Schibsted

Logging done by our code is normally fed to plog-store. In dev they're
then passed on to regress-plog-writer which stores them inside $dest/logs.
In production they're instead passed on to logstash for further processing.

If plog-store is not running the sysloghook module will kick in in dev.
It does a similar task to regress-plog-writer but stores plain text instead
of json. In this case logs are also often printed to stderr, which never
happens if plog-store is running.

## How to log

## C/C++

Options are: `log_printf`, `plog`, `xwarn/xerr`.

### `log_printf`

Use this for generic messages, from debug printing to critical errors.
This is meant to be easy to use without having to think about tags etc.

### `plog`

Example:

	if (debug_level() >= LOG_DEBUG)
		plog_dict_pairs(logging_plog_ctx(), PLOG_DEBUG, "msg", "my message", "mykey", "myvalue", NULL);

Use for structured/tagged logging. Here the define `PLOG_DEBUG` is used
to match the tags used by `log_printf`, but any string can be used instead
if you wish a custom key. Using "msg" with a human readable message is the
convention when using the standard tag keys.

### `xwarn/xerr`

Only use this before logging has been setup, e.g. during option parsing, or
from `sebase-util` which doesn't have any other logging options. They'll be
passed to plog-store with a generic `log` tag.

## Go

Options are: `plog.Level.Printf`, `plog.LogDict`, `log.Printf`

### `plog.Level.Printf`

This closely matches the `log_printf` in being easy to use for generic messages.

### `plog.LogDict`

Example:

	plog.LogDict("mymsg", "msg", "my message", "mykey", "myvalue")

Use for structured/tagged logging. `mymsg` is the top level tag. Also exists
on the levels, then the top level tag is skipped:

	plog.Debug.LogDict("msg", "my message", "mykey", "myvalue")

Using the "msg" key with a human readable message is the convention when using
level based logging, but can be skipped for custom tags.

There are other options in plog as well, such as `plog.Log` if you wish to log
any single value, or `plog.Default.OpenDict` / `OpenList` to open sub contexts.

### `log.Printf`

Use in situations where plog is not available, e.g. before calling sapp.Init
or in packages that shouldn't depend on plog. They'll be passed to plog-store
with a `INFO` tag, similar to `plog.Info.Printf`.
