// Copyright 2018 Schibsted

package main

import "C"
import "runtime"

//export test_go
func test_go() {
	// Not sure this tests anything important.
	runtime.LockOSThread()
	println("OK")
	runtime.UnlockOSThread()
}
