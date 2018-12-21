// Copyright 2018 Schibsted

package fd_pool

import (
	"context"
	"fmt"
	"net/http"
	"testing"
	"time"
)

func testDialer(t *testing.T, pool FdPool) {
	err := pool.AddSingle(context.TODO(), "localhost", "tcp", fmt.Sprintf("localhost:%d", port), 1, 1000*time.Second)
	if err != nil {
		t.Fatal(err)
	}

	// "8080" is automatically mapped to "port" by default.
	req, err := http.NewRequest("GET", "http://localhost:8080/", nil)
	if err != nil {
		t.Fatal(err)
	}

	client := http.Client{
		Transport: &http.Transport{
			DialContext: Dialer(pool),
		},
	}

	resp, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}

	if resp.StatusCode != 404 {
		t.Errorf("Expected status 404, got %v", resp.Status)
	}
}
