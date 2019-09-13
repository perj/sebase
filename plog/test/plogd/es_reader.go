// Copyright 2018 Schibsted

package main

import (
	"io"
	"net/http"
	"os"
	"strconv"
	"sync"
)

func main() {
	port := os.Args[1]
	n, _ := strconv.Atoi(os.Args[2])

	var wg sync.WaitGroup
	wg.Add(n)

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, `{}`)
	})
	http.HandleFunc("/_bulk", func(w http.ResponseWriter, r *http.Request) {
		io.Copy(os.Stdout, r.Body)
		io.WriteString(w, `
{"took":543,"errors":false,"items":[{"index":{"_index":"test","_type":"_doc","_id":"CfGkrWwB6nj95KrkBboj","_version":1,"result":"created","_shards":{"total":2,"successful":1,"failed":0},"_seq_no":0,"_primary_term":1,"status":201}}]}
`)
		wg.Done()
	})

	go http.ListenAndServe(":"+port, nil)
	wg.Wait()
}
