// Copyright 2018 Schibsted

#include "sbp/goinit.h"
#include "macros.h"
#include "linker_set.h"

#ifdef __APPLE__
// For some reason the weak import don't work properly on OS X.
// I won't investigate this further right now as we also can't disable the
// auto-load of the go environment on OS X right now.
// As we don't know if the Go runtime exists false seems like the safe choice.

bool
has_go_runtime(void) {
	return false;
}

bool
init_go_runtime(int argc, char *const *argv) {
	return false;
}
#else
extern void GO_INIT_LIB_SYMBOL(int argc, char *const*argv, char **environ) WEAK;
extern void _cgo_wait_runtime_init_done(void) WEAK;
extern char **environ;

bool
has_go_runtime(void) {
	return GO_INIT_LIB_SYMBOL != NULL && _cgo_wait_runtime_init_done != NULL;
}

bool
init_go_runtime(int argc, char *const *argv) {
	if (!GO_INIT_LIB_SYMBOL || !_cgo_wait_runtime_init_done)
		return false;
	GO_INIT_LIB_SYMBOL(argc, argv, environ);
	_cgo_wait_runtime_init_done();
	return true;
}
#endif
