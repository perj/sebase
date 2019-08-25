// Copyright 2019 Schibsted

package plogd

import (
	"encoding/json"
	"time"
)

// LogMessage is the type used for when a message is contructed and is ready to
// be sent off.
// Struct order matters for Marshaling, keep Timestamp first.
type LogMessage struct {
	Timestamp time.Time   `json:"@timestamp"`
	Prog      string      `json:"prog"`
	Type      string      `json:"type"`
	Message   interface{} `json:"message"`
	// Use a pointer to work around MarshalJSON encoding Time even if zero value
	StartTimestamp *time.Time `json:"start_timestamp,omitempty"`
	Host           string     `json:"host,omitempty"`

	// Extra fields added by filters.
	KV map[string]interface{} `json:"-"`
}

// MarshalJSON creates a proper JSON type, incorporating the KV map as needed.
func (msg *LogMessage) MarshalJSON() ([]byte, error) {
	if len(msg.KV) == 0 {
		type alias LogMessage
		return json.Marshal((*alias)(msg))
	}
	return json.Marshal(msg.ToMap())
}

// ToMap creates a map[string]interface{} representation of the message.
// This function will modify msg.KV and return it, to save the need
// of copying it.
func (msg *LogMessage) ToMap() map[string]interface{} {
	if msg.KV == nil {
		msg.KV = make(map[string]interface{})
	}
	// Assuming it's ok to overwrite KV.
	msg.KV["@timestamp"] = msg.Timestamp
	msg.KV["prog"] = msg.Prog
	msg.KV["type"] = msg.Type
	msg.KV["message"] = msg.Message
	if msg.StartTimestamp != nil {
		msg.KV["start_timestamp"] = msg.StartTimestamp
	}
	if msg.Host != "" {
		msg.KV["host"] = msg.Host
	}
	return msg.KV
}
