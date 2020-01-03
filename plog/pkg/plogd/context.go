package plogd

import (
	"context"
)

type contextKey int

const (
	contextKeyProg contextKey = iota
)

// ContextWithProg creates a new context with prog stored.
func ContextWithProg(ctx context.Context, prog string) context.Context {
	return context.WithValue(ctx, contextKeyProg, prog)
}

// ContextProg returns the prog stored in ctx, if any. Otherwise empty string.
func ContextProg(ctx context.Context) string {
	v, _ := ctx.Value(contextKeyProg).(string)
	return v
}
