/*
 * ONQL C Driver - Implementation
 *
 * Synchronous blocking TCP client.  Cross-platform: POSIX sockets + Winsock.
 */

#include "onql.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Platform abstraction                                                */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define SOCK_CLOSE(s) closesocket(s)
  #define SOCK_ERR      SOCKET_ERROR
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define SOCK_CLOSE(s) close(s)
  #define SOCK_ERR      (-1)
#endif

/* ------------------------------------------------------------------ */
/* Protocol constants                                                  */
/* ------------------------------------------------------------------ */

#define EOM       '\x04'   /* end-of-message   */
#define DELIM     '\x1E'   /* field delimiter   */
#define RID_LEN   8        /* hex chars in a request id */

/* Initial receive-buffer size (grows as needed). */
#define INITIAL_BUF_SIZE 4096

/* ------------------------------------------------------------------ */
/* Internal client structure                                           */
/* ------------------------------------------------------------------ */

struct onql_client {
    sock_t sock;
    char  *buf;       /* receive buffer  */
    size_t buf_len;   /* bytes of data   */
    size_t buf_cap;   /* allocated size  */
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
static int wsa_inited = 0;

static int ensure_wsa(void) {
    if (!wsa_inited) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return -1;
        wsa_inited = 1;
    }
    return 0;
}
#endif

/* Generate an 8-character lowercase hex request ID using rand(). */
static void generate_request_id(char out[RID_LEN + 1]) {
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < RID_LEN; i++)
        out[i] = hex[rand() % 16];
    out[RID_LEN] = '\0';
}

/* Duplicate a region of memory as a NUL-terminated string. */
static char *mem_dup_str(const char *src, size_t len) {
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, src, len);
    s[len] = '\0';
    return s;
}

/* Ensure the receive buffer has room for at least `need` more bytes. */
static int buf_reserve(struct onql_client *c, size_t need) {
    size_t required = c->buf_len + need;
    if (required <= c->buf_cap)
        return 0;
    size_t new_cap = c->buf_cap * 2;
    if (new_cap < required)
        new_cap = required;
    char *tmp = (char *)realloc(c->buf, new_cap);
    if (!tmp) return -1;
    c->buf     = tmp;
    c->buf_cap = new_cap;
    return 0;
}

/* Send `len` bytes, handling partial writes. Returns 0 on success. */
static int send_all(sock_t sock, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

onql_client *onql_connect(const char *host, int port) {
#ifdef _WIN32
    if (ensure_wsa() != 0) return NULL;
#endif

    /* Resolve host */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return NULL;

    sock_t sock = SOCK_INVALID;
    struct addrinfo *rp;
    for (rp = res; rp; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == SOCK_INVALID) continue;
        if (connect(sock, rp->ai_addr, (int)rp->ai_addrlen) == 0)
            break;
        SOCK_CLOSE(sock);
        sock = SOCK_INVALID;
    }
    freeaddrinfo(res);

    if (sock == SOCK_INVALID)
        return NULL;

    struct onql_client *c = (struct onql_client *)calloc(1, sizeof(*c));
    if (!c) { SOCK_CLOSE(sock); return NULL; }

    c->sock    = sock;
    c->buf_cap = INITIAL_BUF_SIZE;
    c->buf     = (char *)malloc(c->buf_cap);
    c->buf_len = 0;

    if (!c->buf) {
        SOCK_CLOSE(sock);
        free(c);
        return NULL;
    }
    return c;
}

onql_response *onql_send_request(onql_client *client,
                                  const char *keyword,
                                  const char *payload)
{
    if (!client || client->sock == SOCK_INVALID)
        return NULL;

    /* Build the outgoing frame:  rid \x1E keyword \x1E payload \x04 */
    char rid[RID_LEN + 1];
    generate_request_id(rid);

    size_t kw_len  = strlen(keyword);
    size_t pl_len  = strlen(payload);
    /* rid + delim + keyword + delim + payload + EOM */
    size_t msg_len = RID_LEN + 1 + kw_len + 1 + pl_len + 1;
    char *msg = (char *)malloc(msg_len);
    if (!msg) return NULL;

    char *p = msg;
    memcpy(p, rid, RID_LEN);             p += RID_LEN;
    *p++ = DELIM;
    memcpy(p, keyword, kw_len);          p += kw_len;
    *p++ = DELIM;
    memcpy(p, payload, pl_len);          p += pl_len;
    *p++ = EOM;

    if (send_all(client->sock, msg, msg_len) != 0) {
        free(msg);
        return NULL;
    }
    free(msg);

    /* Read until we get a response whose RID matches ours. */
    for (;;) {
        /* Scan the buffer for a complete message (terminated by EOM). */
        char *eom = (char *)memchr(client->buf, EOM, client->buf_len);
        if (eom) {
            size_t frame_len = (size_t)(eom - client->buf);

            /* Split on DELIM — expect exactly 3 parts. */
            char *f1 = (char *)memchr(client->buf, DELIM, frame_len);
            if (!f1) goto discard;
            char *f2 = (char *)memchr(f1 + 1, DELIM,
                                       frame_len - (size_t)(f1 + 1 - client->buf));
            if (!f2) goto discard;

            size_t rid_len = (size_t)(f1 - client->buf);
            size_t src_len = (size_t)(f2 - f1 - 1);
            size_t pay_len = (size_t)(eom - f2 - 1);

            /* Check if this response matches our request ID. */
            if (rid_len == RID_LEN && memcmp(client->buf, rid, RID_LEN) == 0) {
                onql_response *resp = (onql_response *)calloc(1, sizeof(*resp));
                if (!resp) return NULL;

                resp->request_id = mem_dup_str(client->buf, rid_len);
                resp->source     = mem_dup_str(f1 + 1, src_len);
                resp->payload    = mem_dup_str(f2 + 1, pay_len);

                /* Remove consumed frame (including EOM byte) from buffer. */
                size_t consumed = frame_len + 1;
                memmove(client->buf, client->buf + consumed,
                        client->buf_len - consumed);
                client->buf_len -= consumed;
                return resp;
            }

discard:
            /* Not our RID or malformed — discard this frame and keep going. */
            {
                size_t consumed = frame_len + 1;
                memmove(client->buf, client->buf + consumed,
                        client->buf_len - consumed);
                client->buf_len -= consumed;
            }
            continue;  /* re-scan buffer before reading more */
        }

        /* No complete message in buffer — read more data from socket. */
        if (buf_reserve(client, 4096) != 0)
            return NULL;

        int n = recv(client->sock, client->buf + client->buf_len,
                     (int)(client->buf_cap - client->buf_len), 0);
        if (n <= 0)
            return NULL;  /* connection closed or error */
        client->buf_len += (size_t)n;
    }
}

void onql_response_free(onql_response *res) {
    if (!res) return;
    free(res->request_id);
    free(res->source);
    free(res->payload);
    free(res);
}

void onql_close(onql_client *client) {
    if (!client) return;
    if (client->sock != SOCK_INVALID)
        SOCK_CLOSE(client->sock);
    free(client->buf);
    free(client);
}

/* ================================================================== *
 *  ORM-style helpers                                                  *
 * ================================================================== */

void onql_free_string(char *s) { free(s); }

/* Append the raw bytes `src` (of length `src_len`) to the dynamic
 * string *dst_ptr with capacity tracking. Returns 0 on success. */
static int str_append(char **dst_ptr, size_t *len, size_t *cap,
                      const char *src, size_t src_len) {
    if (*len + src_len + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 128 : *cap * 2;
        while (new_cap < *len + src_len + 1) new_cap *= 2;
        char *tmp = (char *)realloc(*dst_ptr, new_cap);
        if (!tmp) return -1;
        *dst_ptr = tmp;
        *cap = new_cap;
    }
    memcpy(*dst_ptr + *len, src, src_len);
    *len += src_len;
    (*dst_ptr)[*len] = '\0';
    return 0;
}

/* Append a JSON-escaped string literal: "...". Returns 0 on success. */
static int json_append_string(char **dst, size_t *len, size_t *cap,
                              const char *s) {
    if (str_append(dst, len, cap, "\"", 1) != 0) return -1;
    if (s) {
        const char *p = s;
        while (*p) {
            char esc[8];
            size_t n = 0;
            switch (*p) {
                case '"':  n = 2; esc[0]='\\'; esc[1]='"'; break;
                case '\\': n = 2; esc[0]='\\'; esc[1]='\\'; break;
                case '\n': n = 2; esc[0]='\\'; esc[1]='n'; break;
                case '\r': n = 2; esc[0]='\\'; esc[1]='r'; break;
                case '\t': n = 2; esc[0]='\\'; esc[1]='t'; break;
                default:   n = 1; esc[0]=*p; break;
            }
            if (str_append(dst, len, cap, esc, n) != 0) return -1;
            p++;
        }
    }
    return str_append(dst, len, cap, "\"", 1);
}

/* Parse "db.table" or "db.table.id" into dynamically-allocated pieces.
 * Returns 0 on success; caller must free *out_db and *out_table (and
 * *out_id when non-empty). */
static int parse_path(const char *path, int require_id,
                      char **out_db, char **out_table, char **out_id) {
    *out_db = NULL; *out_table = NULL; *out_id = NULL;
    if (!path || !*path) return -1;
    const char *dot1 = strchr(path, '.');
    if (!dot1 || dot1 == path) return -1;
    const char *dot2 = strchr(dot1 + 1, '.');
    size_t db_len = (size_t)(dot1 - path);
    size_t table_len = dot2 ? (size_t)(dot2 - dot1 - 1) : strlen(dot1 + 1);
    if (table_len == 0) return -1;
    *out_db    = mem_dup_str(path, db_len);
    *out_table = mem_dup_str(dot1 + 1, table_len);
    if (dot2) {
        *out_id = mem_dup_str(dot2 + 1, strlen(dot2 + 1));
    } else {
        *out_id = mem_dup_str("", 0);
    }
    if (!*out_db || !*out_table || !*out_id) return -1;
    if (require_id && (*out_id)[0] == '\0') return -1;
    return 0;
}

/* Internal: send `keyword` with a pre-built payload, returning the raw
 * response payload string (caller frees). Returns NULL on transport
 * failure. */
static char *send_and_extract(onql_client *client,
                              const char *keyword,
                              const char *payload) {
    onql_response *res = onql_send_request(client, keyword, payload);
    if (!res) return NULL;
    char *payload_copy = res->payload ? mem_dup_str(res->payload, strlen(res->payload)) : NULL;
    onql_response_free(res);
    return payload_copy;
}

/* Find the JSON value span after the key "key" in `raw`. Returns a
 * pointer to the first character of the value (which may be a quoted
 * string, number, object, array, or literal), and writes its length to
 * `*out_len`. Returns NULL if the key is missing. */
static const char *find_json_value(const char *raw, const char *key, size_t *out_len) {
    size_t key_len = strlen(key);
    const char *p = raw;
    while ((p = strstr(p, "\"")) != NULL) {
        if (strncmp(p + 1, key, key_len) == 0 && p[1 + key_len] == '"') {
            const char *c = p + 2 + key_len;
            while (*c && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++;
            if (*c != ':') { p++; continue; }
            c++;
            while (*c && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) c++;
            if (!*c) return NULL;
            const char *start = c;
            if (*c == '"') {
                c++;
                while (*c) {
                    if (*c == '\\' && *(c + 1)) c += 2;
                    else if (*c == '"') { c++; break; }
                    else c++;
                }
            } else if (*c == '{' || *c == '[') {
                char open = *c, close = (*c == '{') ? '}' : ']';
                int depth = 1; c++;
                while (*c && depth > 0) {
                    if (*c == '"') {
                        c++;
                        while (*c) {
                            if (*c == '\\' && *(c + 1)) c += 2;
                            else if (*c == '"') { c++; break; }
                            else c++;
                        }
                    } else {
                        if (*c == open)  depth++;
                        if (*c == close) depth--;
                        c++;
                    }
                }
            } else {
                while (*c && *c != ',' && *c != '}' && *c != ']' &&
                       *c != ' ' && *c != '\t' && *c != '\n' && *c != '\r')
                    c++;
            }
            *out_len = (size_t)(c - start);
            return start;
        }
        p++;
    }
    return NULL;
}

int onql_process_result(const char *raw, char **out_data, char **out_error) {
    if (out_data)  *out_data = NULL;
    if (out_error) *out_error = NULL;
    if (!raw) return -1;

    size_t err_len = 0;
    const char *err_val = find_json_value(raw, "error", &err_len);
    int has_error = 0;
    if (err_val && err_len > 0) {
        if (!(err_len == 4 && strncmp(err_val, "null", 4) == 0) &&
            !(err_len == 5 && strncmp(err_val, "false", 5) == 0) &&
            !(err_len == 2 && strncmp(err_val, "\"\"", 2) == 0)) {
            has_error = 1;
            if (out_error) {
                const char *s = err_val;
                size_t n = err_len;
                if (n >= 2 && s[0] == '"' && s[n - 1] == '"') { s++; n -= 2; }
                *out_error = mem_dup_str(s, n);
            }
        }
    }

    size_t data_len = 0;
    const char *data_val = find_json_value(raw, "data", &data_len);
    if (out_data && data_val) {
        *out_data = mem_dup_str(data_val, data_len);
    }

    return has_error ? -1 : 0;
}

/* Build: {"db":"...","table":"...","records":<records_json>} */
static char *build_insert_payload(const char *db, const char *table,
                                  const char *records_json) {
    char *s = NULL; size_t len = 0, cap = 0;
    if (str_append(&s, &len, &cap, "{\"db\":", 6) != 0) goto err;
    if (json_append_string(&s, &len, &cap, db ? db : "") != 0) goto err;
    if (str_append(&s, &len, &cap, ",\"table\":", 9) != 0) goto err;
    if (json_append_string(&s, &len, &cap, table ? table : "") != 0) goto err;
    if (str_append(&s, &len, &cap, ",\"records\":", 11) != 0) goto err;
    if (str_append(&s, &len, &cap, records_json ? records_json : "null",
                   strlen(records_json ? records_json : "null")) != 0) goto err;
    if (str_append(&s, &len, &cap, "}", 1) != 0) goto err;
    return s;
err:
    free(s); return NULL;
}

static char *build_write_payload(const char *db, const char *table,
                                 const char *records_json,
                                 const char *query_json,
                                 const char *protopass,
                                 const char *ids_json,
                                 int include_records) {
    char *s = NULL; size_t len = 0, cap = 0;
    const char *pp  = protopass ? protopass : "default";
    const char *ids = ids_json  ? ids_json  : "[]";
    const char *q   = query_json ? query_json : "null";

    if (str_append(&s, &len, &cap, "{\"db\":", 6) != 0) goto err;
    if (json_append_string(&s, &len, &cap, db ? db : "") != 0) goto err;
    if (str_append(&s, &len, &cap, ",\"table\":", 9) != 0) goto err;
    if (json_append_string(&s, &len, &cap, table ? table : "") != 0) goto err;
    if (include_records) {
        if (str_append(&s, &len, &cap, ",\"records\":", 11) != 0) goto err;
        if (str_append(&s, &len, &cap,
                       records_json ? records_json : "null",
                       strlen(records_json ? records_json : "null")) != 0) goto err;
    }
    if (str_append(&s, &len, &cap, ",\"query\":", 9) != 0) goto err;
    if (str_append(&s, &len, &cap, q, strlen(q)) != 0) goto err;
    if (str_append(&s, &len, &cap, ",\"protopass\":", 13) != 0) goto err;
    if (json_append_string(&s, &len, &cap, pp) != 0) goto err;
    if (str_append(&s, &len, &cap, ",\"ids\":", 7) != 0) goto err;
    if (str_append(&s, &len, &cap, ids, strlen(ids)) != 0) goto err;
    if (str_append(&s, &len, &cap, "}", 1) != 0) goto err;
    return s;
err:
    free(s); return NULL;
}

char *onql_insert(onql_client *client, const char *path,
                  const char *record_json, char **out_error) {
    if (out_error) *out_error = NULL;
    if (!client) return NULL;
    char *db = NULL, *table = NULL, *id = NULL;
    if (parse_path(path, 0, &db, &table, &id) != 0) {
        free(db); free(table); free(id);
        return NULL;
    }
    char *payload = build_insert_payload(db, table, record_json);
    free(db); free(table); free(id);
    if (!payload) return NULL;
    char *raw = send_and_extract(client, "insert", payload);
    free(payload);
    if (!raw) return NULL;
    char *data = NULL;
    onql_process_result(raw, &data, out_error);
    free(raw);
    return data;
}

/* Build an "ids":["<id>"] JSON snippet. Caller frees. */
static char *ids_array_single(const char *id) {
    char *s = NULL; size_t len = 0, cap = 0;
    if (str_append(&s, &len, &cap, "[", 1) != 0) goto err;
    if (json_append_string(&s, &len, &cap, id) != 0) goto err;
    if (str_append(&s, &len, &cap, "]", 1) != 0) goto err;
    return s;
err:
    free(s); return NULL;
}

char *onql_update(onql_client *client, const char *path,
                  const char *record_json, const char *protopass,
                  char **out_error) {
    if (out_error) *out_error = NULL;
    if (!client) return NULL;
    char *db = NULL, *table = NULL, *id = NULL;
    if (parse_path(path, 1, &db, &table, &id) != 0) {
        free(db); free(table); free(id);
        return NULL;
    }
    char *ids_json = ids_array_single(id);
    char *payload = ids_json
        ? build_write_payload(db, table, record_json, "\"\"", protopass, ids_json, 1)
        : NULL;
    free(db); free(table); free(id); free(ids_json);
    if (!payload) return NULL;
    char *raw = send_and_extract(client, "update", payload);
    free(payload);
    if (!raw) return NULL;
    char *data = NULL;
    onql_process_result(raw, &data, out_error);
    free(raw);
    return data;
}

char *onql_delete(onql_client *client, const char *path,
                  const char *protopass, char **out_error) {
    if (out_error) *out_error = NULL;
    if (!client) return NULL;
    char *db = NULL, *table = NULL, *id = NULL;
    if (parse_path(path, 1, &db, &table, &id) != 0) {
        free(db); free(table); free(id);
        return NULL;
    }
    char *ids_json = ids_array_single(id);
    char *payload = ids_json
        ? build_write_payload(db, table, NULL, "\"\"", protopass, ids_json, 0)
        : NULL;
    free(db); free(table); free(id); free(ids_json);
    if (!payload) return NULL;
    char *raw = send_and_extract(client, "delete", payload);
    free(payload);
    if (!raw) return NULL;
    char *data = NULL;
    onql_process_result(raw, &data, out_error);
    free(raw);
    return data;
}

char *onql_onql(onql_client *client, const char *query,
                const char *protopass, const char *ctxkey,
                const char *ctxvalues_json, char **out_error) {
    if (out_error) *out_error = NULL;
    if (!client) return NULL;
    const char *pp   = protopass ? protopass : "default";
    const char *ck   = ctxkey ? ctxkey : "";
    const char *ctxv = ctxvalues_json ? ctxvalues_json : "[]";

    char *payload = NULL; size_t len = 0, cap = 0;
    if (str_append(&payload, &len, &cap, "{\"query\":", 9) != 0) goto err;
    if (json_append_string(&payload, &len, &cap, query ? query : "") != 0) goto err;
    if (str_append(&payload, &len, &cap, ",\"protopass\":", 13) != 0) goto err;
    if (json_append_string(&payload, &len, &cap, pp) != 0) goto err;
    if (str_append(&payload, &len, &cap, ",\"ctxkey\":", 10) != 0) goto err;
    if (json_append_string(&payload, &len, &cap, ck) != 0) goto err;
    if (str_append(&payload, &len, &cap, ",\"ctxvalues\":", 13) != 0) goto err;
    if (str_append(&payload, &len, &cap, ctxv, strlen(ctxv)) != 0) goto err;
    if (str_append(&payload, &len, &cap, "}", 1) != 0) goto err;

    char *raw = send_and_extract(client, "onql", payload);
    free(payload);
    if (!raw) return NULL;
    char *data = NULL;
    onql_process_result(raw, &data, out_error);
    free(raw);
    return data;
err:
    free(payload);
    return NULL;
}

char *onql_build(const char *query, const char *const *values,
                 const int *is_string, int n_values) {
    if (!query) return NULL;
    char *out = mem_dup_str(query, strlen(query));
    if (!out) return NULL;

    for (int i = 0; i < n_values; i++) {
        if (!values[i]) continue;
        char placeholder[16];
        int ph_len = snprintf(placeholder, sizeof(placeholder), "$%d", i + 1);
        if (ph_len <= 0) continue;

        const char *val = values[i];
        size_t val_len = strlen(val);
        int quote = is_string ? is_string[i] : 0;
        size_t rep_len = val_len + (quote ? 2 : 0);

        /* Replace every occurrence. */
        for (;;) {
            char *pos = strstr(out, placeholder);
            if (!pos) break;
            size_t prefix_len = (size_t)(pos - out);
            size_t suffix_len = strlen(pos + ph_len);
            size_t total = prefix_len + rep_len + suffix_len + 1;
            char *new_buf = (char *)malloc(total);
            if (!new_buf) { free(out); return NULL; }
            memcpy(new_buf, out, prefix_len);
            if (quote) new_buf[prefix_len] = '"';
            memcpy(new_buf + prefix_len + (quote ? 1 : 0), val, val_len);
            if (quote) new_buf[prefix_len + 1 + val_len] = '"';
            memcpy(new_buf + prefix_len + rep_len, pos + ph_len, suffix_len);
            new_buf[total - 1] = '\0';
            free(out);
            out = new_buf;
        }
    }
    return out;
}
