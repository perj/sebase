// Copyright 2018 Schibsted

#ifndef BASE_FILE_UTILS_H
#define BASE_FILE_UTILS_H

#include <stdio.h>
#include <sys/types.h>

/*
 * Read from input until EOF or error, write contents to a file created with
 * mkstemp using tmpl.
 * Returns the number of bytes written, or -1 on error.
 * You should call unlink(tmpl); when done using the file.
 */
ssize_t write_to_tmpfile(char *tmpl, FILE *input);

#endif /*BASE_FILE_UTILS_H*/
