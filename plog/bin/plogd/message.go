// Copyright 2018 Schibsted

package main

import (
	"encoding/json"
	"time"
)

// Struct order matters, keep Timestamp first.
type LogMessage struct {
	Timestamp time.Time   `json:"@timestamp"`
	Prog      string      `json:"prog"`
	Type      string      `json:"type"`
	Data      interface{} `json:"message"`
	// Use a pointer to work around MarshalJSON encoding Time even if zero value
	StartTimestamp *time.Time `json:"start_timestamp,omitempty"`
	Host           string     `json:"host"`

	// Extra fields added by filters.
	KV map[string]interface{} `json:"-"`
}

func (msg *LogMessage) ToJSON() ([]byte, error) {
	if len(msg.KV) == 0 {
		return json.Marshal(msg)
	}
	// Assuming it's ok to overwrite KV.
	msg.KV["prog"] = msg.Prog
	msg.KV["type"] = msg.Type
	msg.KV["message"] = msg.Data
	msg.KV["@timestamp"] = msg.Timestamp
	if msg.StartTimestamp != nil {
		msg.KV["start_timestamp"] = msg.StartTimestamp
	}
	msg.KV["host"] = msg.Host
	return json.Marshal(msg.KV)
}
