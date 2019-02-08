#include "controller-events.h"

#include <sys/epoll.h>

#define CONTROLLER_EPOLL_TIMEOUT_MS 2000
#define CONTROLLER_NUM_FDS 64

int
event_e_init(union ctrl_event_e *e) {
	e->epollfd = epoll_create(CONTROLLER_NUM_FDS);
	return 0;
}

int
event_e_close(union ctrl_event_e *e) {
	close(e->epollfd);
	e->epollfd = -1;
	return 0;
}

int
event_e_add(union ctrl_event_e *e, struct event_handler *event_handler, int fd) {
	struct epoll_event event;
	event.events = EPOLLIN|EPOLLHUP;
	event.data.ptr = event_handler;
	return epoll_ctl(e->epollfd, EPOLL_CTL_ADD, fd, &event);
}

int
event_e_remove(union ctrl_event_e *e, int fd) {
	struct epoll_event event;
	return epoll_ctl(e->epollfd, EPOLL_CTL_DEL, fd, &event);
}

int
event_e_triggered(union ctrl_event_e *e, int fd) {
	return event_e_remove(e, fd);
}

int
event_e_handle(union ctrl_event_e *e, struct ctrl *ctrl) {
	struct epoll_event events[CONTROLLER_NUM_FDS];
	int nfds;
	/* Wake up every x to handle quit */
	if ((nfds = epoll_wait(e->epollfd, events, CONTROLLER_NUM_FDS, CONTROLLER_EPOLL_TIMEOUT_MS)) < 0)
		return -1;

	for (int i = 0; i < nfds; i++) {
		struct event_handler *event_handler = events[i].data.ptr;
		event_handler->cb(event_handler, ctrl);
	}
	return 0;
}
