// Copyright 2018 Schibsted

#ifndef SETTINGS_H
#define SETTINGS_H

struct vtree_chain;

void get_settings(struct vtree_chain *vchain, const char *setting,
	     const char *(*key_lookup)(const char *setting, const char *key, void *cbdata), 
	     void (*set_value)(const char *setting, const char *key, const char *value, void *cbdata),
	     void *cbdata);

#endif /*SETTINGS_H*/
