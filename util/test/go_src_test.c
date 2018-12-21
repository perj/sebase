// Copyright 2018 Schibsted

#include "sbp/goinit.h"
#include "gosrc.h"

int
main(int argc, char *argv[]) {
	init_go_runtime(argc, argv);
	test_go();
}
