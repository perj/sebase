package plogd

import (
	"context"
)

type contextKey int

const (
	contextKeyProg contextKey = iota
)

func ContextWithProg(ctx context.Context, prog string) context.Context {
	return context.WithValue(ctx, contextKeyProg, prog)
}

func ContextProg(ctx context.Context) string {
	v, _ := ctx.Value(contextKeyProg).(string)
	return v
}
