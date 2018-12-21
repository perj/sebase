// Copyright 2018 Schibsted

#include "bconf.h"
#include "vtree.h"

enum req_utf8 {
	RUTF8_NOCHECK,
	RUTF8_REQUIRE,
	RUTF8_FALLBACK_LATIN1,
};

/* Used to bundle data for the mod_templates/parse_request path */
struct parse_qs_options {
	enum tristate escape_html;
	enum tristate defaulted_escape_html; /* template/type default */
};

struct parse_cb_data {
	void *cb_data;
	struct parse_qs_options *options;
};

/* Call parse_cb for each key=value separated by &, qs will be destroyed */
void parse_query_string(char *qs, void (*parse_cb)(struct parse_cb_data *cb_data, char *key, int klen, char *value, int vlen),
		void *cb_data, struct vtree_chain *vars, int unsafe, enum req_utf8 req_utf8,
		struct parse_qs_options *options);
void parse_query_string_value(const char *key, char *value, int vlen,
		void (*parse_cb)(struct parse_cb_data*, char*, int, char*, int),
		void *cb_data, struct vtree_chain *vars, int req_utf8, struct parse_qs_options *options);

