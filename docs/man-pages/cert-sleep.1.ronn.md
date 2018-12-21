cert-sleep(1) -- Sleep until certificate expiry
===============================================

## SYNOPSIS

`cert-sleep` [`-min-exit-early` duration] [`-max-exit-early` duration]
[`--`] pem-file... [`--` cmd]

## DESCRIPTION

`cert-sleep` will scan all pem encoded files given as arguments. Any
certificates found are checked for their expiration time. `cert-sleep` will
print out the earliest time it finds and sleep until then. You can also
give a directory in which case all files in the directory will be checked.

If the option `-min-exit-early` is used, that duration is subtracted and
the tool exits early. If `-max-exit-early` is given, a random exit time
is chosen in the allowed interval.

You can add `--` and then a command to execute at the end of the command line.
`cert-sleep` will exit directly if the program exits. When the timer expires
the program is first sent SIGTERM and then SIGKILL after 5 seconds. `cert-sleep`
will wait for the program to exit before exiting.

## OPTIONS

* `-min-exit-early` duration:
	Exit this duration early. The duration is given as one or more
	numbers suffixed with the unit, one of `h`, `m`, `s`.
	For example `-min-exit-early 3h30m`. Defaults to `0`.

* `-max-exit-early` duration:
	If given, choose a random time in the allowed range to exit.
	Also a duration similar to `-min-exit-early`. No default.

## EXIT STATUS

Exits 0 after sleeping, or directly if the exit time is earlier than the
current time, or if the executed program exits with a 0 exit code.

Exits 1 if no certificates are found or invalid intervals were given in the
options. A diagnostics message will be printed to stderr.

Exits 2 if the executed program exits with non-zero exit code.

## EXAMPLE

	cert-sleep -min-exit-early 10m -max-exit-early 1h *.pem

## COPYRIGHT

Copyright 2018 Schibsted
