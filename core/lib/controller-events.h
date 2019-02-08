#pragma once

#include "sbp/queue.h"

struct ctrl;

struct event_handler {
	void (*cb)(struct event_handler *, struct ctrl *);
	int fd;
	struct tls *tls;
	TAILQ_ENTRY(event_handler) event_entry;
};

union ctrl_event_e {
	int epollfd;
};

int event_e_init(union ctrl_event_e *e);
int event_e_close(union ctrl_event_e *e);

int event_e_add(union ctrl_event_e *e, struct event_handler *event_handler, int fd);
int event_e_remove(union ctrl_event_e *e, int fd);
int event_e_triggered(union ctrl_event_e *e, int fd);

int event_e_handle(union ctrl_event_e *e, struct ctrl *ctrl);
