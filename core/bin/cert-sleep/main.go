// Copyright 2018 Schibsted

// Will sleep until the expiration any of the PEM encoded certificates read
// from all files given as arguments.
//
// Can optionally exit early depending on options.
package main

import (
	"crypto/rand"
	"crypto/x509"
	"encoding/pem"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"math/big"
	"os"
	"os/exec"
	"path/filepath"
	"syscall"
	"time"
)

var (
	minExitEarly time.Duration
	maxExitEarly time.Duration
)

func main() {
	flag.DurationVar(&minExitEarly, "min-exit-early", minExitEarly, "Exit at least this duration before any of the certificates expires. A random duration between min and max is used, or the exact time if only min is given.")
	flag.DurationVar(&maxExitEarly, "max-exit-early", maxExitEarly, "Exit at most this duration before any of the certificates expires. A random duration between min and max is used.")
	flag.Parse()

	var runUntil time.Time
	var lowest string
	var prog []string
	for idx, path := range flag.Args() {
		if path == "--" {
			prog = flag.Args()[idx+1:]
			break
		}
		st, err := os.Stat(path)
		if err != nil {
			log.Fatal(err)
		}
		if st.IsDir() {
			filepath.Walk(path, func(path string, info os.FileInfo, err error) error {
				if info.IsDir() {
					return nil
				}
				na := ParseCerts(path)
				if !na.IsZero() && (runUntil.IsZero() || na.Before(runUntil)) {
					runUntil = na
					lowest = path
				}
				return nil
			})
		} else {
			na := ParseCerts(path)
			if !na.IsZero() && (runUntil.IsZero() || na.Before(runUntil)) {
				runUntil = na
				lowest = path
			}
		}
	}

	if runUntil.IsZero() {
		fmt.Fprintf(os.Stderr, "No certificates found")
		os.Exit(1)
	}

	if minExitEarly < 0 {
		log.Fatal("-min-exit-early is negative")
	}
	if maxExitEarly == 0 {
		maxExitEarly = minExitEarly
	}
	if maxExitEarly < minExitEarly {
		log.Fatal("-max-exit-early is lower than -min-exit-early")
	}

	runUntil = SubExitEarly(runUntil, minExitEarly, maxExitEarly)

	fmt.Printf("Lowest not after from %s\n", lowest)
	fmt.Printf("Will sleep until %v\n", runUntil)

	var progch chan int
	var cmd *exec.Cmd
	if len(prog) > 0 {
		cmd = exec.Command(prog[0], prog[1:]...)
		cmd.Stdin = os.Stdin
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Start(); err != nil {
			log.Fatal(err)
		}
		progch = make(chan int)
		go func() {
			err := cmd.Wait()
			ecode := 0
			if eerr, _ := err.(*exec.ExitError); eerr != nil {
				ecode = 2
			}
			progch <- ecode
		}()
	}

	select {
	case <-time.After(runUntil.Sub(time.Now())):
		if cmd != nil {
			cmd.Process.Signal(syscall.SIGTERM)
			select {
			case <-progch:
			case <-time.After(5 * time.Second):
				cmd.Process.Kill()
				<-progch
			}
		}
		break
	case ecode := <-progch:
		os.Exit(ecode)
	}
}

func ParseCerts(path string) (notAfter time.Time) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		log.Fatal(err)
	}
	for len(data) > 0 {
		var pemdata *pem.Block
		pemdata, data = pem.Decode(data)
		if pemdata == nil {
			log.Printf("No PEM data found in %v", path)
			return
		}
		if pemdata.Type != "CERTIFICATE" {
			continue
		}

		cert, err := x509.ParseCertificate(pemdata.Bytes)
		if err != nil {
			log.Print(err)
			continue
		}

		if notAfter.IsZero() || cert.NotAfter.Before(notAfter) {
			notAfter = cert.NotAfter
		}
	}
	return
}

func SubExitEarly(t time.Time, min, max time.Duration) time.Time {
	if t.IsZero() || min < 0 || max < 0 || max < min {
		return t
	}
	max -= min
	if max > 0 {
		r, err := rand.Int(rand.Reader, big.NewInt(int64(max)))
		if err != nil {
			panic(err)
		}
		max = time.Duration(r.Int64())
	}
	return t.Add(-min - max)
}
