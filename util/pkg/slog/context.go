package slog

import "context"

type ctxKeyFilter struct{}

// ContextWithFilter adds a filter functions to the context, allowing you to augment
// kvs passed to CtxKVsMap (possibly via CtxError etc.). All filters are called
// the order they were added (the top level context first). This allows
// sub-context filters to modify the parent ones further.
// A typical filter simply prepends or appends more key-value pairs.
func ContextWithFilter(ctx context.Context, f func(kvs []interface{}) []interface{}) context.Context {
	filters, _ := ctx.Value(ctxKeyFilter{}).([]func(kvs []interface{}) []interface{})
	// Make sure to copy to not be overwritten by sibling contexts.
	filters = append(filters[:len(filters):len(filters)], f)
	return context.WithValue(ctx, ctxKeyFilter{}, filters)
}

// CtxKVsMap checks ctx for any filters added via ContextWithFilter and calls
// those if found. It then calls KVsMap with the result.
func CtxKVsMap(ctx context.Context, kvs ...interface{}) map[string]interface{} {
	kvs = applyKVFilters(ctx, kvs)
	return KVsMap(kvs...)
}

func applyKVFilters(ctx context.Context, kvs []interface{}) []interface{} {
	// Check for forgetting to add ...
	if len(kvs) == 1 {
		iarr, _ := kvs[0].([]interface{})
		if iarr != nil {
			kvs = iarr
		}
	}
	if ctx != nil {
		filters, _ := ctx.Value(ctxKeyFilter{}).([]func(kvs []interface{}) []interface{})
		for _, f := range filters {
			kvs = f(kvs)
		}
	}
	return kvs
}
