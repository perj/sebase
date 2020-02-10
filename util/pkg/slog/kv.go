package slog

import (
	"fmt"
)

// KV can be used instead of a key-value pair in KVsMap function and any
// function that use it, e.g. DefaultLogger.LogMsg.
type KV map[string]interface{}

// ToMap returns kvs, it implements the ToMap interface.
func (kvs KV) ToMap() map[string]interface{} {
	return kvs
}

// KVError can be used to customize error printing via this packge.
// If implemented, it should write values to m. The primary log message
// should be written to the key given, but other keys can be added.
// Make sure to do so in a responsible manner though, to not overwrite
// other keys such as "msg". You can for example prefix your keys with
// your package name.
//
// This interface is experimental and might be removed/changed in minor
// versions.
type KVError interface {
	KVError(m map[string]interface{}, key string)
}

// ToMap is used to check arguments passed to KVsMap. If this function is
// implemented the keys and values returned are added to the KVsMap return
// value.
type ToMap interface {
	ToMap() map[string]interface{}
}

// KVsMap converts a set of key-value pairs into a map. Each argument read
// is type checked. If it's a string, the next argument is used as a value.
// If it's a map[string]interface{} then the contents is copied to the return value.
// Same thing if it implements the ToMap interface, then the map contents is
// copied.
// If it's an error then the key is set to "error" and the argument is used as
// a value.
// If none of the types match, fmt.Sprint is used to convert to a string and the
// next argument is used as value.
// Error interface values are special handled, converted to the error message.
// If they implement KVError that function is called instead.
func KVsMap(kvs ...interface{}) map[string]interface{} {
	// Check for forgetting to add ...
	if len(kvs) == 1 {
		iarr, _ := kvs[0].([]interface{})
		if iarr != nil {
			kvs = iarr
		}
	}
	m := make(map[string]interface{}, len(kvs)/2)
	for i := 0; i < len(kvs); i++ {
		switch k := kvs[i].(type) {
		case string:
			var v interface{}
			if i+1 < len(kvs) {
				i++
				v = kvs[i]
			}
			kvsMapV(m, k, v)
		case map[string]interface{}:
			for mk, v := range k {
				kvsMapV(m, mk, v)
			}
		case ToMap:
			for mk, v := range k.ToMap() {
				kvsMapV(m, mk, v)
			}
		case error:
			kvsMapV(m, "error", k)
		default:
			mk := fmt.Sprint(k)
			var v interface{}
			if i+1 < len(kvs) {
				i++
				v = kvs[i]
			}
			kvsMapV(m, mk, v)
		}
	}
	return m
}

func kvsMapV(m map[string]interface{}, k string, v interface{}) {
	switch v := v.(type) {
	case string:
		m[k] = v
	case KVError:
		v.KVError(m, k)
	case error:
		m[k] = v.Error()
	case fmt.Stringer:
		m[k] = v.String()
	default:
		m[k] = v
	}
}
