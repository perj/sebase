// +build linux

package fd_pool

import (
	"context"
	"net"
	"syscall"

	"github.com/schibsted/sebase/util/pkg/slog"
	"golang.org/x/sys/unix"
)

func checkDeadConnection(ctx context.Context, netc net.Conn) (dead bool) {
	// If possible, test that the connection is valid.
	// I've tested using netc.Write for this, but it didn't work.
	// Instead call syscall.Poll directly. This is Linux only
	// because it only works with the POLLRDHUP flag which is linux specific.
	type rawconn interface {
		SyscallConn() (syscall.RawConn, error)
	}
	if sc, ok := netc.(rawconn); ok {
		if scc, err := sc.SyscallConn(); err == nil {
			scc.Control(func(fd uintptr) {
				pfd := []unix.PollFd{
					{int32(fd), unix.POLLHUP | unix.POLLRDHUP, 0},
				}
				n, err := unix.Poll(pfd, 0)
				if n == 0 {
					return
				}
				dead = true
				if err != nil {
					slog.CtxDebug(ctx, "Not returning dead connection", "error", err)
				} else {
					slog.CtxDebug(ctx, "Not returning dead connection", "eof", true)
				}
			})
		}
	}
	return
}
