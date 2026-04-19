# ONQL C Driver

Official C client library for the ONQL database server.

## Installation

The C driver is not currently published to a binary package registry; install
it from source. Each tagged release also attaches prebuilt archives to its
[GitHub Release page](https://github.com/ONQL/onqlclient-c/releases) for
Linux, macOS, and Windows.

### Build from source

```bash
git clone https://github.com/ONQL/onqlclient-c.git
cd onqlclient-c
mkdir build && cd build
cmake ..
make
sudo make install
```

### CMake `FetchContent`

Consume directly from GitHub in your own project:

```cmake
include(FetchContent)
FetchContent_Declare(
    onql
    GIT_REPOSITORY https://github.com/ONQL/onqlclient-c.git
    GIT_TAG        v0.1.0   # or: main
)
FetchContent_MakeAvailable(onql)

target_link_libraries(your_target PRIVATE onql)
```

## Quick Start

```c
#include <stdio.h>
#include "onql.h"

int main() {
    onql_client *client = onql_connect("localhost", 5656);
    if (!client) {
        fprintf(stderr, "Connection failed\n");
        return 1;
    }

    // Execute a query
    onql_response *res = onql_send_request(client, "onql",
        "{\"db\":\"mydb\",\"table\":\"users\",\"query\":\"name = \\\"John\\\"\"}");
    if (res) {
        printf("%s\n", res->payload);
        onql_response_free(res);
    }

    onql_close(client);
    return 0;
}
```

## API Reference

### Connection

```c
// Connect to server
onql_client *onql_connect(const char *host, int port);

// Close connection and free resources
void onql_close(onql_client *client);
```

### Requests

```c
// Send request and wait for response (caller must free with onql_response_free)
onql_response *onql_send_request(onql_client *client, const char *keyword, const char *payload);

// Free a response
void onql_response_free(onql_response *res);
```

### Response Structure

```c
typedef struct {
    char *request_id;
    char *source;
    char *payload;
} onql_response;
```

## Direct ORM-style API

On top of raw `onql_send_request`, the driver exposes convenience helpers for
the `insert` / `update` / `delete` / `onql` operations. Each helper builds the
standard payload envelope for you, parses the `{error, data}` response, and
returns the decoded `data` as a newly heap-allocated string that the caller
must free with `onql_free_string()`.

Because the driver is dependency-free, every JSON-valued parameter (record,
context values) is passed as a **pre-serialized JSON string** — use your
favourite C JSON library (cJSON, jansson, …) to serialize.

`path` is a dotted string identifying what you're operating on:

| Path shape | Meaning |
|------------|---------|
| `"mydb.users"` | Table (used by `onql_insert`) |
| `"mydb.users.u1"` | Record id `u1` (used by `onql_update` / `onql_delete`) |

### Error handling

Every helper accepts an `out_error` out-parameter. If the server returned a
non-empty `error` field, the helper returns `NULL` and `*out_error` is set to
a newly allocated error message (caller frees with `onql_free_string`). On
transport failure, both the return value and `*out_error` are `NULL`.

### `onql_insert`

```c
char *onql_insert(onql_client *client,
                  const char *path,         /* e.g. "mydb.users" */
                  const char *record_json,  /* a single JSON object */
                  char **out_error);
```

Insert a **single** record.

### `onql_update`

```c
char *onql_update(onql_client *client,
                  const char *path,         /* e.g. "mydb.users.u1" */
                  const char *record_json,  /* JSON object of fields to update */
                  const char *protopass,    /* NULL -> "default" */
                  char **out_error);
```

### `onql_delete`

```c
char *onql_delete(onql_client *client,
                  const char *path,         /* e.g. "mydb.users.u1" */
                  const char *protopass,    /* NULL -> "default" */
                  char **out_error);
```

### `onql_onql`

```c
char *onql_onql(onql_client *client,
                const char *query,
                const char *protopass,       /* NULL -> "default" */
                const char *ctxkey,          /* NULL -> ""        */
                const char *ctxvalues_json,  /* NULL -> "[]"      */
                char **out_error);
```

### `onql_build`

```c
char *onql_build(const char *query,
                 const char *const *values,
                 const int *is_string,
                 int n_values);
```

Replace `$1`, `$2`, … placeholders. For each value, if `is_string[i]` is
non-zero the value is double-quoted when substituted; otherwise it is inlined
verbatim (numbers, booleans). Pass `is_string = NULL` to treat every value
as raw.

### `onql_process_result`

```c
int onql_process_result(const char *raw, char **out_data, char **out_error);
```

Parse the standard `{"error":"…","data":…}` envelope. Returns `0` on
success, `-1` when the server reported an error or the envelope could not be
parsed. Out-parameters (when non-NULL) receive newly allocated strings that
the caller must free with `onql_free_string`.

### `onql_free_string`

```c
void onql_free_string(char *s);
```

Free a string returned by any of the helpers above.

### Full example

```c
#include <stdio.h>
#include "onql.h"

int main(void) {
    onql_client *c = onql_connect("localhost", 5656);
    if (!c) return 1;

    char *err = NULL;

    char *data = onql_insert(c, "mydb.users",
        "{\"id\":\"u1\",\"name\":\"John\",\"age\":30}", &err);
    onql_free_string(data);
    onql_free_string(err);

    /* Build and run a query */
    const char *vals[]  = { "John", "18" };
    const int   quote[] = { 1, 0 };
    char *q = onql_build(
        "select * from mydb.users where name = $1 and age > $2",
        vals, quote, 2);

    err = NULL;
    data = onql_onql(c, q, NULL, NULL, NULL, &err);
    if (data) printf("%s\n", data);
    onql_free_string(data);
    onql_free_string(err);
    onql_free_string(q);

    onql_update(c, "mydb.users.u1", "{\"age\":31}", NULL, &err);
    onql_delete(c, "mydb.users.u1", NULL, &err);

    onql_close(c);
    return 0;
}
```

## Protocol

The client communicates over TCP using a delimiter-based message format:

```
<request_id>\x1E<keyword>\x1E<payload>\x04
```

- `\x1E` — field delimiter
- `\x04` — end-of-message marker

## License

MIT
