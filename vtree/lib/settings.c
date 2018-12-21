// Copyright 2018 Schibsted

#include <string.h>

#include "settings.h"
#include "vtree.h"

/**
  Handle a "settings" bconf structure.
  Parses a 'key[:value]' comma separated list, handling string literals within quotation marks, and escaping.
*/
static void
settings_add_values(const char *setting, 
		    const char *value,
		    void (*set_value)(const char *setting, const char *key, const char *value, void *cbdata),
		    void *cbdata) {
	char *keyval;
	char *out;
	char *valstart;
	const char *f;
	const char *fend;
	int in_quote = 0;
	int len;
	
	if (!setting || !value)
		return;
	len = strlen(value) + 1;
	keyval = alloca(len);

	out = keyval;
	f = value;
	valstart = NULL;
	fend = f + len;

	while (f != fend) {

		if (*f == '"') {
			in_quote ^= 1;
		} else if (*f == '\\' && f != fend) {
			/* Just because we're so special, we want to allow \" within "literal strings" to output a quotation mark.
			   The idea is that users will expect this to work. */
			if (in_quote && (*(f+1) == '"')) { 
				*out++ = '"'; ++f;
			} else {
				*out++ = *(++f);
			}
		} else if ((!in_quote && *f == ',') || *f == '\0' ) {
			*out = '\0';
			if (keyval && *keyval)
				set_value(setting, keyval, valstart, cbdata);
			valstart = NULL;
			out = keyval;
		} else if (!in_quote && *f == ':') {
			*out++ = '\0';
			valstart = out;
		} else {
			*out++ = *f;
		}
		++f;
	}
}

static void
settings_add_node(const char *setting, 
		  struct vtree_chain *vchain,
		  const char *(*key_lookup)(const char *setting, const char *key, void *cbdata), 
		  void (*set_value)(const char *setting, const char *key, const char *value, void *cbdata),
		  void *cbdata) {
	int i;
	int j;
	int f_continue = 0;
	struct vtree_loop_var loop = {0};

	if (!setting || !vchain)
		return;

	vtree_fetch_nodes(vchain, &loop, NULL);
	for (j = 0 ; j < loop.len ; j++) {
		struct vtree_chain *c = &loop.l.vlist[j];
		struct vtree_loop_var keyloop;

		vtree_fetch_values(c, &keyloop, "keys", VTREE_LOOP, NULL);

		/* If set for features.<num> level, don't be satisfied with the first match, but continue the search */
		f_continue = vtree_getint(c, "continue", NULL);

		if (!keyloop.len) {
			const char *def = vtree_get(c, "default", NULL);

			if (keyloop.cleanup)
				keyloop.cleanup(&keyloop);

			if (def && def[0]) {
				settings_add_values(setting, def, set_value, cbdata);
				if (!f_continue)
					break;
			}
			continue;
		}

		for (i = 0 ; i < keyloop.len ; i++) {
			const char *val;

			if (!c || !keyloop.l.list[i]) {
				c = NULL;
				break;
			}

			if ((val = key_lookup(setting, keyloop.l.list[i], cbdata))) {
				struct vtree_chain n = {0};

				if (vtree_getnode(c, &n, val, NULL)) {
					vtree_free(c);
					*c = n;
				} else if (vtree_getnode(c, &n, "*", NULL)) {
					/* XXX hrm, this might be a bit bad. */
					vtree_free(c);
					*c = n;
				} else
					c = NULL;
			} else
				c = NULL;
		}

		if (keyloop.cleanup)
			keyloop.cleanup(&keyloop);

		if (c) {
			settings_add_values(setting, vtree_get(c, "value", NULL), set_value, cbdata);
			if ( !f_continue )
				break;
		}
	}
	if (loop.cleanup)
		loop.cleanup(&loop);
}

void
get_settings(struct vtree_chain *vchain,
	     const char *setting, 
	     const char *(*key_lookup)(const char *setting, const char *key, void *cbdata), 
	     void (*set_value)(const char *setting, const char *key, const char *value, void *cbdata),
	     void *cbdata) {

	if (vchain) {
		if (setting && setting[0] != '\0') {
			struct vtree_chain n = {0};
		       
			if (vtree_getnode(vchain, &n, setting, NULL)) {
				settings_add_node(setting, &n, key_lookup, set_value, cbdata);
				vtree_free(&n);
			}
		} else {
			int i;
			struct vtree_loop_var kloop = {0};
			struct vtree_loop_var vloop = {0};

			vtree_fetch_keys(vchain, &kloop, NULL);
			vtree_fetch_nodes(vchain, &vloop, NULL);

			for (i = 0 ; i < kloop.len && i < vloop.len; i++) {
				settings_add_node(kloop.l.list[i], &vloop.l.vlist[i],
						key_lookup, set_value, cbdata);
			}
			if (kloop.cleanup)
				kloop.cleanup(&kloop);
			if (vloop.cleanup)
				vloop.cleanup(&vloop);
		}
	}
}

