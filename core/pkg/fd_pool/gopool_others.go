//+build !linux

package fd_pool

import (
	"net"
	"syscall"

	"github.com/schibsted/sebase/util/pkg/slog"
	"golang.org/x/sys/unix"
)

func checkDeadConnection(netc net.Conn) (dead bool) {
	// If possible, test that the connection is valid.
	// I've tested using netc.Write for this, but it didn't work.
	// Instead call syscall.Poll directly.
	type rawconn interface {
		SyscallConn() (syscall.RawConn, error)
	}
	if sc, ok := netc.(rawconn); ok {
		if scc, err := sc.SyscallConn(); err == nil {
			scc.Control(func(fd uintptr) {
				pfd := []unix.PollFd{
					{int32(fd), unix.POLLHUP, 0},
				}
				n, err := unix.Poll(pfd, 0)
				if n == 0 {
					return
				}
				dead = true
				if err != nil {
					slog.Debug("Not returning dead connection", "error", err)
				} else {
					slog.Debug("Not returning dead connection", "eof", true)
				}
			})
		}
	}
	return
}
