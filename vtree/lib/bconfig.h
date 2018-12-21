// Copyright 2018 Schibsted

#ifndef CONFIG_H
#define CONFIG_H

struct bconf_node;

struct bconf_node* config_init(const char *filename);
int config_free();

/* usually requires 'blocket_id' to be present in *root */
int load_bconf_file(const char *appl, struct bconf_node **root, const char *filename);

int config_merge_bconf(struct bconf_node **root, struct bconf_node *bconf, const char *host, const char *appl);

#endif
