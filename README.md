# ONQL C Driver

Official C client library for the ONQL database server.

## Installation

The C driver is not currently published to a binary package registry; install
it from source. Each tagged release also attaches prebuilt archives to its
[GitHub Release page](https://github.com/ONQL/onqlclient-c/releases).

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

```cmake
include(FetchContent)
FetchContent_Declare(
    onql
    GIT_REPOSITORY https://github.com/ONQL/onqlclient-c.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(onql)

target_link_libraries(your_target PRIVATE onql)
```

## Quick Start

```c
#include <stdio.h>
#include "onql.h"

int main(void) {
    onql_client *c = onql_connect("localhost", 5656);
    if (!c) return 1;

    char *err = NULL;

    char *data = onql_insert(c, "mydb", "users",
        "{\"id\":\"u1\",\"name\":\"John\",\"age\":30}", &err);
    onql_free_string(data);
    onql_free_string(err);

    char *q = onql_build("mydb.users[id=$1].id",
                         (const char*[]){"u1"},
                         (const int[]){1}, 1);

    err = NULL;
    data = onql_update(c, "mydb", "users", "{\"age\":31}",
                       q, NULL, NULL, &err);
    onql_free_string(data);
    onql_free_string(err);
    onql_free_string(q);

    onql_delete(c, "mydb", "users", "", NULL, "[\"u1\"]", &err);

    onql_close(c);
    return 0;
}
```

## API Reference

### Connection

```c
onql_client *onql_connect(const char *host, int port);
void onql_close(onql_client *client);
```

### Raw request

```c
onql_response *onql_send_request(onql_client *client,
                                 const char *keyword,
                                 const char *payload);
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
standard payload envelope, parses the `{error, data}` response, and returns
the decoded `data` as a newly heap-allocated string (free with
`onql_free_string()`).

Because the driver is dependency-free, every JSON-valued parameter is passed
as a **pre-serialized JSON string** — use cJSON, jansson, or similar.

`db` is passed explicitly to `insert` / `update` / `delete`. `onql` takes a
fully-qualified ONQL expression.

`query` arguments are **ONQL expression strings**, e.g.
`mydb.users[id="u1"].id`.

### Error handling

Every helper accepts an `out_error` out-parameter. If the server returned a
non-empty `error`, the helper returns `NULL` and `*out_error` is set to a
newly allocated error string. On transport failure, both are `NULL`.

### `onql_insert`

```c
char *onql_insert(onql_client *client,
                  const char *db,
                  const char *table,
                  const char *record_json,   /* a single JSON object */
                  char **out_error);
```

### `onql_update`

```c
char *onql_update(onql_client *client,
                  const char *db,
                  const char *table,
                  const char *record_json,
                  const char *query,         /* ONQL expression, or "" */
                  const char *protopass,     /* NULL -> "default" */
                  const char *ids_json,      /* NULL -> "[]" */
                  char **out_error);
```

### `onql_delete`

```c
char *onql_delete(onql_client *client,
                  const char *db,
                  const char *table,
                  const char *query,
                  const char *protopass,
                  const char *ids_json,
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

Replace `$1`, `$2`, … placeholders. If `is_string[i]` is non-zero the value
is double-quoted when substituted; otherwise inlined verbatim.

### `onql_process_result`

```c
int onql_process_result(const char *raw, char **out_data, char **out_error);
```

### `onql_free_string`

```c
void onql_free_string(char *s);
```

## Protocol

```
<request_id>\x1E<keyword>\x1E<payload>\x04
```

- `\x1E` — field delimiter
- `\x04` — end-of-message marker

## License

MIT
