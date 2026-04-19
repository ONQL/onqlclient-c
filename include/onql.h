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
 * All string fields are heap-allocated; free with onql_response_free().
 */
typedef struct {
    char *request_id;
    char *source;
    char *payload;
} onql_response;

/** Opaque client handle. */
typedef struct onql_client onql_client;

/**
 * Connect to an ONQL server.
 *
 * @param host  Server hostname or IP address.
 * @param port  Server port number.
 * @return      Client handle, or NULL on failure.
 */
onql_client *onql_connect(const char *host, int port);

/**
 * Send a request and block until the matching response arrives.
 *
 * @param client   Connected client handle.
 * @param keyword  The ONQL keyword (e.g. "onql", "subscribe").
 * @param payload  The request payload string.
 * @return         Heap-allocated response, or NULL on error.
 *                 Caller must free with onql_response_free().
 */
onql_response *onql_send_request(onql_client *client,
                                  const char *keyword,
                                  const char *payload);

/**
 * Free a response returned by onql_send_request().
 */
void onql_response_free(onql_response *res);

/**
 * Close the connection and free the client handle.
 */
void onql_close(onql_client *client);

/* ================================================================== *
 *  Direct ORM-style API (setup / insert / update / delete / onql /
 *  build / process_result)
 *
 *  All helpers below wrap `onql_send_request` with the standard ONQL
 *  payload envelopes and parse the `{error, data}` response.  Because
 *  the driver is dependency-free, every JSON-valued parameter
 *  (records, query, ids, ctxvalues) is passed as a pre-serialized
 *  JSON string; use your favourite JSON library (cJSON, jansson, ...)
 *  to serialise.
 * ================================================================== */

/**
 * Set the default database name used by onql_insert / onql_update /
 * onql_delete / onql_onql. The string is copied internally.
 *
 * @param client  Connected client handle.
 * @param db      Database name.
 */
void onql_setup(onql_client *client, const char *db);

/**
 * Insert one record or an array of records.
 *
 * @param client        Connected client handle.
 * @param table         Target table name.
 * @param records_json  JSON-serialized record object, or array of records.
 * @param out_error     If non-NULL and the server returned an `error`
 *                      field, receives a newly allocated error string
 *                      (caller must free with onql_free_string()).
 *                      Set to NULL when there is no error.
 * @return              Newly allocated string containing the decoded
 *                      `data` field from the server envelope (caller
 *                      must free with onql_free_string()); or NULL on
 *                      transport / protocol failure. If an `error`
 *                      field was present, NULL is returned and
 *                      *out_error is populated.
 */
char *onql_insert(onql_client *client,
                  const char *table,
                  const char *records_json,
                  char **out_error);

/**
 * Update records in `table` matching `query_json`.
 *
 * @param client        Connected client handle.
 * @param table         Target table name.
 * @param records_json  JSON object of fields to update.
 * @param query_json    JSON query.
 * @param protopass     Proto-pass profile (e.g. "default"). If NULL,
 *                      "default" is used.
 * @param ids_json      JSON array of explicit record IDs (e.g. "[]").
 *                      If NULL, "[]" is used.
 * @param out_error     Optional error out-param (see onql_insert).
 * @return              Data string; caller frees with onql_free_string().
 */
char *onql_update(onql_client *client,
                  const char *table,
                  const char *records_json,
                  const char *query_json,
                  const char *protopass,
                  const char *ids_json,
                  char **out_error);

/**
 * Delete records in `table` matching `query_json`.
 * Parameters mirror onql_update (minus `records_json`).
 */
char *onql_delete(onql_client *client,
                  const char *table,
                  const char *query_json,
                  const char *protopass,
                  const char *ids_json,
                  char **out_error);

/**
 * Execute a raw ONQL query.
 *
 * @param client         Connected client handle.
 * @param query          ONQL query text.
 * @param protopass      Proto-pass profile, or NULL for "default".
 * @param ctxkey         Context key, or NULL for "".
 * @param ctxvalues_json JSON array of context values (e.g. "[]"), or NULL.
 * @param out_error      Optional error out-param.
 * @return               Data string; caller frees with onql_free_string().
 */
char *onql_onql(onql_client *client,
                const char *query,
                const char *protopass,
                const char *ctxkey,
                const char *ctxvalues_json,
                char **out_error);

/**
 * Replace $1, $2, ... placeholders in `query` with the supplied string
 * values. Values flagged as strings (is_string=1) are double-quoted
 * when substituted; values with is_string=0 are inlined verbatim
 * (useful for numbers and booleans).
 *
 * @param query       Query text containing $1, $2, ... placeholders.
 * @param values      Array of `n_values` replacement strings.
 * @param is_string   Array of `n_values` flags (non-zero = quote as string).
 *                    May be NULL to treat every value as raw (unquoted).
 * @param n_values    Number of values supplied.
 * @return            Newly allocated result string; caller must free
 *                    with onql_free_string().
 */
char *onql_build(const char *query,
                 const char *const *values,
                 const int *is_string,
                 int n_values);

/**
 * Parse the standard `{"error":"...","data":"..."}` envelope returned
 * by insert/update/delete/onql responses.
 *
 * @param raw         Response payload string.
 * @param out_data    Receives a newly allocated copy of the `data`
 *                    field (may be a JSON literal like "null"). Caller
 *                    frees with onql_free_string(). NULL-out on error.
 * @param out_error   Receives a newly allocated copy of the `error`
 *                    field if non-empty; otherwise NULL. Caller frees
 *                    with onql_free_string().
 * @return            0 on success (no `error` field), -1 when an error
 *                    was reported or the envelope could not be parsed.
 */
int onql_process_result(const char *raw, char **out_data, char **out_error);

/**
 * Free a string returned by any of the ORM-style helpers
 * (onql_insert, onql_update, onql_delete, onql_onql, onql_build,
 * or via the out_error / out_data parameters of onql_process_result).
 */
void onql_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* ONQL_H */
