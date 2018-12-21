// Copyright 2018 Schibsted

package sdr

type MessageType int

const (
	// Used to signal that there are no more immediate messages, and
	// changes should thus be applied now.
	// Only index is set in this message.
	EndOfBatch MessageType = iota

	// Updates a value for a specific host key, all fields are set
	Update

	// Deletes either a host or a value. If the entire host is deleted,
	// key will be empty string, otherwise it indicates the delete value.
	// Value field is always empty.
	Delete

	// Delete all the hosts, usually sent together with new updates for
	// all of them, if there's any left.
	// Only index is set in this message.
	Flush
)

// Messages sent from SD sources to the SourceConn.  You read until you receive
// an EndOfBatch message and then commit the changes.
//
// The index is a sequence that increases for each batch, but might skip
// numbers.
type Message struct {
	Type  MessageType
	Index uint64

	HostKey string
	Key     string
	Value   string
}
