# Logging

Copyright 2018 Schibsted

Logging done by our code is normally fed to plog-store. In regress they're
then passed on to regress-plog-writer which stores them inside $dest/logs.
In production they're instead passed on to logstash for further processing.

If plog-store is not running the sysloghook module will kick in in regress.
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
		plog_dict_pairs(logging_plog_ctx(), "mymsg", "mykey", "myvalue", NULL);

Use for structured/tagged logging. `mymsg` is the top level tag. You can also use
a define like `PLOG_DEBUG` to match the tags used by `log_printf`.

### `xwarn/xerr`

Only use this before logging has been setup, e.g. during option parsing, or
from `sebase-util` which doesn't have any other logging options. They'll be
passed to plog-store with a generic `log` tag.

## Go

Options are: `plog.Level.Printf`, `plog.IfEnabled`, `log.Printf`

### `plog.Level.Printf`

This closely matches the `log_printf` in being easy to use for generic messages.

### `plog.IfEnabled`

Example:

	plog.IfEnabled(plog.Debug).LogDict("mymsg", "mykey", "myvalue")

Use for structured/tagged logging. `mymsg` is the top level tag. You can
skip the level check by using `plog.Default` instead.

### `log.Printf`

Use in situations where plog is not available, e.g. before calling sapp.Init
or in packages that shouldn't depend on plog. They'll be passed to plog-store
with a `INFO` tag, similar to `plog.Info.Printf`.
