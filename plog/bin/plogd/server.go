// Copyright 2018 Schibsted

package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strings"
	"syscall"
)

type server struct {
	dataStore  *DataStorage
	mux        *http.ServeMux
	fileWriter *jsonFileWriter
}

func (s *server) queryState(w http.ResponseWriter, req *http.Request) {
	query := strings.Split(strings.TrimPrefix(req.URL.Path, "/state/"), ".")
	if len(query) < 1 {
		http.NotFound(w, req)
		return
	}
	prog := query[0]
	path := query[1:]

	// Ask to access the state on the progStoreHandler thread, to avoid race conditions on the map.
	ch := make(chan []byte)
	b, err := s.dataStore.CallbackState(prog, func(state map[string]interface{}) {
		var value interface{} = state
		for _, step := range path {
			m, ok := value.(map[string]interface{})
			if !ok {
				ch <- nil
				return
			}
			value, ok = m[step]
			if !ok {
				ch <- nil
				return
			}
		}
		data, err := json.Marshal(value)
		if err != nil {
			log.Print("Failed to query state", err)
			ch <- nil
			return
		}
		ch <- data
	})
	switch {
	case b:
		break
	case err == ErrorProgNotFound:
		http.NotFound(w, req)
		return
	case err == ErrorTooManyConcurrentRequests:
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	default:
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	data := <-ch
	if data == nil {
		http.NotFound(w, req)
		return
	}

	fmt.Fprintf(w, `{"value":%s}`, data)
}

func (s *server) stop(w http.ResponseWriter, req *http.Request) {
	sigCh <- syscall.SIGTERM
	<-quitDoneCh
}

func (s *server) rotate(w http.ResponseWriter, req *http.Request) {
	err := s.fileWriter.rotate()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

func (s *server) run(addr string) {
	s.mux = http.NewServeMux()
	s.mux.HandleFunc("/state/", s.queryState)
	s.mux.HandleFunc("/stop", s.stop)
	if s.fileWriter != nil {
		s.mux.HandleFunc("/rotate", s.rotate)
	}
	go func() {
		log.Fatal(http.ListenAndServe(addr, s.mux))
	}()
}
