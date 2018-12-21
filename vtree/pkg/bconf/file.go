// Copyright 2018 Schibsted

package bconf

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"
)

// A line that failed to parse in ReadFile.
type SyntaxError struct {
	Line string
}

func (se SyntaxError) Error() string {
	return fmt.Sprintf("Syntax error, missing = near %s", se.Line)
}

// Reads a bconf file and adds the data to bconf.
// If allowEnv is true, os.ExpandEnv is called on
// the values.
// This is similar to InitFromFile except it doesn't use C code and
// InitFromFile handles environment variables slightly different.
//
// Lines are on the form
//
// keypath=value
//
// where keypath is a . delimated string and value is any string. Whitespace
// at the start and end of lines are trimmed.
// You can also use a include directive which is a line on the form
//
// include path
//
// That path will then also be read if it exists. It's non-fatal if it doesn't.
// The path is relative to the current file being read.
//
// Lines starting with # are considered comments.
func ReadFile(bconf BconfAdder, path string, allowEnv bool) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	for s.Scan() {
		line := strings.TrimSpace(s.Text())
		switch {
		case line == "":
			break
		case strings.HasPrefix(line, "#"):
			break
		case strings.HasPrefix(line, "include "):
			inc := strings.TrimPrefix(line, "include ")
			if allowEnv {
				inc = os.ExpandEnv(inc)
			}
			if !filepath.IsAbs(inc) {
				inc = filepath.Join(filepath.Dir(path), inc)
			}
			err = ReadFile(bconf, inc, allowEnv)
			// We ignore path errors, such as missing file.
			// It's a feature.
			if err != nil {
				if _, ok := err.(*os.PathError); !ok {
					return err
				}
			}
		case strings.ContainsRune(line, '='):
			kv := strings.SplitN(line, "=", 2)
			k := strings.TrimSpace(kv[0])
			v := strings.TrimSpace(kv[1])
			if allowEnv {
				v = os.ExpandEnv(v)
			}
			err := bconf.Add(k)(v)
			if err != nil {
				return err
			}
		default:
			return SyntaxError{line}
		}
	}
	return s.Err()
}
