#include "controller-events.h"

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#define CONTROLLER_KQ_TIMEOUT_S 2
#define CONTROLLER_NUM_FDS 64

int
event_e_init(union ctrl_event_e *e) {
	e->kqfd = kqueue();
	return 0;
}

int
event_e_close(union ctrl_event_e *e) {
	close(e->kqfd);
	e->kqfd = -1;
	return 0;
}

int
event_e_add(union ctrl_event_e *e, struct event_handler *event_handler, int fd, bool oneshot) {
	struct kevent event;
	int flags = EV_ADD | (oneshot ? EV_ONESHOT : 0);
	EV_SET(&event, fd, EVFILT_READ, flags, 0, 0, event_handler);
	return kevent(e->kqfd, &event, 1, NULL, 0, NULL);
}

int
event_e_remove(union ctrl_event_e *e, int fd) {
	struct kevent event;
	EV_SET(&event, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	return kevent(e->kqfd, &event, 1, NULL, 0, NULL);
}

int
event_e_oneshot_triggered(union ctrl_event_e *e, int fd) {
	// Noop because of EV_ONESHOT.
	return 0;
}

int
event_e_handle(union ctrl_event_e *e, struct ctrl *ctrl) {
	struct kevent events[CONTROLLER_NUM_FDS];
	int nfds;
	struct timespec to = { .tv_sec = CONTROLLER_KQ_TIMEOUT_S };

	/* Wake up every x to handle quit */
	nfds = kevent(e->kqfd, NULL, 0, events, CONTROLLER_NUM_FDS, &to);
	if (nfds < 0)
		return -1;

	for (int i = 0; i < nfds; i++) {
		struct event_handler *event_handler = events[i].udata;
		event_handler->cb(event_handler, ctrl);
	}
	return 0;
}
