/*
 * ONQL C Driver - Public API
 *
 * Synchronous (blocking) TCP client for the ONQL database server.
 * Protocol: messages delimited by \x04, fields delimited by \x1E.
 * Message format: {request_id}\x1E{keyword}\x1E{payload}\x04
 */

#ifndef ONQL_H
#define ONQL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Response returned from onql_send_request().
 */
typedef struct {
    char *request_id;
    char *source;
    char *payload;
} onql_response;

typedef struct onql_client onql_client;

/**
 * Connect to an ONQL server.
 *
 * @return Client handle, or NULL on failure.
 */
onql_client *onql_connect(const char *host, int port);

/**
 * Send a raw request frame and block until the matching response arrives.
 * Caller must free the returned response with onql_response_free().
 */
onql_response *onql_send_request(onql_client *client,
                                  const char *keyword,
                                  const char *payload);

void onql_response_free(onql_response *res);

/**
 * Close the connection and free the client handle.
 */
void onql_close(onql_client *client);

/* ================================================================== *
 *  Direct ORM-style API (insert / update / delete / onql / build /
 *  process_result)
 *
 *  `path` is a dotted string:
 *    "mydb.users"        -> table `users` in database `mydb`
 *    "mydb.users.u1"     -> record with id `u1`
 *
 *  Because the driver is dependency-free, every JSON-valued parameter
 *  (record, ctxvalues) is passed as a pre-serialized JSON string.
 * ================================================================== */

/**
 * Insert a single record at `path`.
 *
 * @param client      Connected client handle.
 * @param path        Table path (e.g. "mydb.users").
 * @param record_json JSON-serialized record object.
 * @param out_error   If non-NULL and the server returned an `error`,
 *                    receives a newly allocated error string (caller
 *                    frees with onql_free_string()); else NULL.
 * @return            Newly allocated string containing the decoded
 *                    `data` field from the server envelope (caller
 *                    frees with onql_free_string()); or NULL on
 *                    transport / protocol failure.
 */
char *onql_insert(onql_client *client,
                  const char *path,
                  const char *record_json,
                  char **out_error);

/**
 * Update the record at `path` (e.g. "mydb.users.u1").
 *
 * @param record_json JSON object of fields to update.
 * @param protopass   Proto-pass profile, or NULL for "default".
 */
char *onql_update(onql_client *client,
                  const char *path,
                  const char *record_json,
                  const char *protopass,
                  char **out_error);

/**
 * Delete the record at `path`.
 *
 * @param protopass   Proto-pass profile, or NULL for "default".
 */
char *onql_delete(onql_client *client,
                  const char *path,
                  const char *protopass,
                  char **out_error);

/**
 * Execute a raw ONQL query.
 *
 * @param protopass      Proto-pass profile, or NULL for "default".
 * @param ctxkey         Context key, or NULL for "".
 * @param ctxvalues_json JSON array of context values (e.g. "[]"), or NULL.
 */
char *onql_onql(onql_client *client,
                const char *query,
                const char *protopass,
                const char *ctxkey,
                const char *ctxvalues_json,
                char **out_error);

/**
 * Replace $1, $2, ... placeholders in `query` with the supplied string
 * values. If is_string[i] is non-zero, the value is double-quoted;
 * otherwise it is inlined verbatim (numbers, booleans).
 * Pass is_string = NULL to treat every value as raw.
 *
 * @return Newly allocated result string; caller frees with onql_free_string().
 */
char *onql_build(const char *query,
                 const char *const *values,
                 const int *is_string,
                 int n_values);

/**
 * Parse the standard `{"error":"...","data":"..."}` envelope.
 *
 * @return 0 on success (no error), -1 when an error was reported or the
 *         envelope could not be parsed. Any non-NULL out-parameters
 *         receive newly allocated strings that the caller must free with
 *         onql_free_string().
 */
int onql_process_result(const char *raw, char **out_data, char **out_error);

/**
 * Free a string returned by any of the ORM-style helpers.
 */
void onql_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* ONQL_H */
