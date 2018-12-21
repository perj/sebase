getbconfvars(1) -- Import bconf formatted configuration file data into shell variables
======================================================================================

## SYNOPSIS

`getbconfvars` [ `-f` file ] [ `-r` root ] [ `-p` prefix ] [ `-k` key ]

## DESCRIPTION

`getbconfvars` reads one or more bconf-style configuration files and list of
`regex` patterns, and outputs matched configuration nodes in a format suitable
to be evaluated by shell scripts.

## OPTIONS

**Generic Program Information**

`-h`, `--help` Print a usage message briefly summarizing these command-line
options, then exit.

**Extraction options**

`-f`, `--file` _file_ Sets the input file to operate on, resets _root_ and
_prefix_.

`-r`, `--root` _node_path_ Sets the configuration node path to use as the root
for lookups.

`-p`, `--prefix` _prefix_ Sets the current _prefix_ to prepend to the returned
values.

`-k`, `--key` _regex_ The _regex_ to match against keys we want to select. Uses
the currently set _file_, _root_ and _prefix_, which should have been set
previously.

## EXAMPLES

Due to the stateful nature of the options, the input `--file` should be
specified first, then any of `--root` and `--prefix` before any `--key`

Any periods in the resulting key/path will be replaced with underscores:

	$ getbconfvars --file trans.conf -k name
	db_pgsql_master_hostname=localhost db_pgsql_master_name=blocketdb
	db_pgsql_slave_hostname=localhost db_pgsql_slave_name=blocketdb

Use `--root` and `--key` to limit the result to the parts you require:

	$ getbconfvars --file trans.conf --prefix pg_ --root db.pgsql.master -k
	'^(name|socket)$' pg_name=blocketdb pg_socket=/dev/shm/regress-user/pgsql0/data

Combine with `eval` to import the data into shell variables:

	$ eval `getbconfvars -f trans.conf -p trans_ -k control_port -k
	max_connections` $ echo $trans_control_port 20207


## COPYRIGHT

Copyright 2018 Schibsted
