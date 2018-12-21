// Copyright 2018 Schibsted

#ifndef PLOG_H
#define PLOG_H

#include "sbp/macros.h"

#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct plog_conn;
struct plog_ctx;

/*
 * There are a few kinds of root contexts: log, state, counter.
 *
 * Log is fed log objects, each object should be send of to the log.
 *
 * States are kept in the log server. They're meant to be long-lived.
 * State updates might be sent of to backends, depending on configuration.
 * If the last state for an app is closed, it will be committed to a backend.
 *
 * Counts are special type of state sessions. Any numbers logged to a count
 * session is considered a delta. When the session is closed, those counts
 * are removed, and the container is also removed if all count sessions to
 * it are.
 *
 * Updates to states are considered patches, E.g. opening a dict will
 * preserve already existing keys in that dict, and only add new ones.
 * Keys can be explicitly deleted though.
 *
 * Usually you pass NULL as conn. The plog_default_conn will in that case
 * be used.
 *
 * For plog_open_state, you can add periods (.) in appname to directly
 * open a sub state. Logs are currently not allowed to do this, there
 * appname must not contain dots.
 * As it's even more common for counts it has a separate parameter for the
 * path, can be NULL or empty string if not needed.
 */

struct plog_ctx *plog_open_log(struct plog_conn *conn, const char *appname);
struct plog_ctx *plog_open_state(struct plog_conn *conn, const char *appname);
struct plog_ctx *plog_open_count(struct plog_conn *conn, const char *appname, int npath, const char *path[]);

/* Close and free ctx. Returns the number of failed writes recorded (hopefully 0). */
int plog_close(struct plog_ctx *ctx);

/*
 * Plog flags to modify the behaviour of the logging.
 * No flags should be used under normal circumstances, use them only if profiling
 * shows they're needed.
 *
 * If you have a high-performance process (e.g. the search engine) where doing a
 * write syscall per log item is too expensive, you can use PLOG_BUFFERED to
 * instead send all the data on plog_close.
 * Note: This breaks the design of keeping the data in the plog-store daemon
 * instead of in the client process. Use only if needed.
 * It also makes the syslog fallback harder to read.
 */
#define PLOG_BUFFERED  (1 << 0)
void plog_set_flags(struct plog_ctx *ctx, int flags);

/* Flush a buffered ctx. Contexts are unbuffered by default. */
void plog_flush(struct plog_ctx *ctx);

/*
 * Cancel a plog, if it was buffered. Otherwise it's too late.
 */
void plog_cancel(struct plog_ctx *ctx);

/* plog uses JSON, which assumes UTF-8. If your input is not UTF-8, call this. */
enum plog_charset {
	PLOG_UTF8,
	PLOG_LATIN1,
	PLOG_LATIN2,
};
void plog_set_global_charset(enum plog_charset cs);

/*
 * For root contexts, the key specifies the overall type of the message.
 * It's what's used to determine the target and also how to interpret the
 * value.
 *
 * In dictionaries, the key is obvious.
 *
 * In lists, key should be NULL. Only appending is supported. If opening
 * a list in state ctx, it will replace the previous list, if any.
 *
 * If key is NULL when needed, PLOG_DEFAULT_KEY is used.
 */

#define PLOG_DEFAULT_KEY "log"

/*
 * Convenience defines to use as keys when logging with syslog like levels.
 */
#define PLOG_EMERG   "EMERG"
#define PLOG_ALERT   "ALERT"
#define PLOG_CRIT    "CRIT"
#define PLOG_ERR     "ERR"
#define PLOG_WARNING "WARNING"
#define PLOG_NOTICE  "NOTICE"
#define PLOG_INFO    "INFO"
#define PLOG_DEBUG   "DEBUG"

struct plog_ctx *plog_open_dict(struct plog_ctx *ctx, const char *key);
struct plog_ctx *plog_open_dict_flags(struct plog_ctx *ctx, const char *key, int flags);
struct plog_ctx *plog_open_list(struct plog_ctx *ctx, const char *key);
struct plog_ctx *plog_open_list_flags(struct plog_ctx *ctx, const char *key, int flags);

void plog_string(struct plog_ctx *ctx, const char *key, const char *value);
void plog_string_len(struct plog_ctx *ctx, const char *key, const char *value, ssize_t vlen);
void plog_string_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, const char *value, ssize_t vlen);
void plog_string_printf(struct plog_ctx *ctx, const char *key, const char *fmt, ...) FORMAT_PRINTF(3, 4);
void plog_string_vprintf(struct plog_ctx *ctx, const char *key, const char *fmt, va_list ap) FORMAT_PRINTF(3, 0);

void plog_int(struct plog_ctx *ctx, const char *key, int value);
void plog_int_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, int value);
void plog_bool(struct plog_ctx *ctx, const char *key, bool value);
void plog_bool_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, bool value);

/* Alternate dict key and value, end with NULL.
 * Note: key parameter is for the parent object, not part of the dictionary.
 * You can use the PLOG_DICTV defines to add non-string values, if needed.
 * For the JSON value, the JSON text is expected in the next parameter.
 * You can use this to e.g. add numbers, but it's up to the caller to make
 * sure the value is valid. Example:
 * plog_dict_pairs(ctx, "mydict", "pikey", PLOG_SYMBOL_JSON, "3.14", NULL);
 * Note: same disclaimer as for plog_json.
 */
void plog_dict_pairs(struct plog_ctx *ctx, const char *key, ...) SENTINEL(0);
#define PLOG_DICTV_NULL  (&plog_dictv[0])
#define PLOG_DICTV_FALSE (&plog_dictv[1])
#define PLOG_DICTV_TRUE  (&plog_dictv[2])
#define PLOG_DICTV_JSON  (&plog_dictv[3])
extern const char plog_dictv[];

void plog_binary(struct plog_ctx *ctx, const char *key, void *data, size_t len);

/* Note: the json is not verified client side, it's up to caller to make sure it's valid, including UTF-8.
 * Otherwise the connection will abort. */
void plog_json(struct plog_ctx *ctx, const char *key, const char *json, ssize_t jsonlen);
void plog_json_klen(struct plog_ctx *ctx, const char *key, ssize_t klen, const char *json, ssize_t jsonlen);
void plog_json_printf(struct plog_ctx *ctx, const char *key, const char *fmt, ...) FORMAT_PRINTF(3, 4);

/* You can only delete from state type objects. */
void plog_delete(struct plog_ctx *ctx, const char *key);

/*
 * Utility function appending to buffer and then calling plog_string
 * on each line found. Note: NOT thread safe.
 */
void plog_string_stream(struct plog_ctx *ctx, const char *key, const char *str);

/* Reset failed writes for ctx and adds them to pctx, which is usually the parent context.
 * log_shutdown will log the number failed writes for the top level contexts.
 */
void plog_move_failed_writes(struct plog_ctx *pctx, struct plog_ctx *ctx);
/* Resets and returns the number of failed writes for ctx. */
int plog_reset_failed_writes(struct plog_ctx *ctx);

/* xerr support, all messages are logged with the default key "log" */
void plog_init_x_err(const char *appname);
void plog_xerr_close(void);

#ifdef __cplusplus
}
#endif

#endif /*PLOG_H*/
