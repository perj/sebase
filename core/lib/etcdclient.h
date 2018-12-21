// Copyright 2018 Schibsted

#ifndef ETCDCLIENT_H
#define ETCDCLIENT_H

struct etcdwatcher;
struct sd_queue;
struct https_state;

/* Note that prefix should always start with / and should also end with / if you want a directory. */
struct etcdwatcher *etcdwatcher_create(const char *prefix, const char *server_url, struct https_state *https);
void etcdwatcher_free(struct etcdwatcher *ec);

void etcdwatcher_set_flush_period(struct etcdwatcher *ec, int seconds);

int etcdwatcher_start(struct etcdwatcher *ec);
int etcdwatcher_stop(struct etcdwatcher *ec);

struct sd_queue *etcdwatcher_add_listen(struct etcdwatcher *ec, const char *path, const int *tpvec, int tpvlen);
void etcdwatcher_remove_listen(struct etcdwatcher *ec, struct sd_queue *queue);

#endif /*ETCDCLIENT_H*/
