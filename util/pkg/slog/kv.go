package slog

import "fmt"

// KV can be used instead of a key-value pair in KVsMap function and any
// function that use it, e.g. DefaultLogger.LogMsg.
type KV map[string]interface{}

// ToMap returns kvs, it implements the ToMap interface.
func (kvs KV) ToMap() map[string]interface{} {
	return kvs
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
// If none of the types match, fmt.Sprint is used to convert to a string and the
// next argument is used as value. Sometimes this happens if you forget to add
// ... to expand the array when calling this function
// Error interface values are special handled, converted to the error message.
func KVsMap(kvs ...interface{}) map[string]interface{} {
	m := make(map[string]interface{}, len(kvs)/2)
	for i := 0; i < len(kvs); i++ {
		switch k := kvs[i].(type) {
		case string:
			var v interface{}
			if i+1 < len(kvs) {
				i++
				v = kvsMapV(kvs[i])
			}
			m[k] = v
		case map[string]interface{}:
			for mk, v := range k {
				m[mk] = kvsMapV(v)
			}
		case ToMap:
			for mk, v := range k.ToMap() {
				m[mk] = kvsMapV(v)
			}
		default:
			mk := fmt.Sprint(k)
			var v interface{}
			if i+1 < len(kvs) {
				i++
				v = kvsMapV(kvs[i])
			}
			m[mk] = v
		}
	}
	return m
}

func kvsMapV(v interface{}) interface{} {
	switch v := v.(type) {
	case error:
		return v.Error()
	}
	return v
}
