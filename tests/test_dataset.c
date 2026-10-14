#include "dataset.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char header[] = "city,city_ascii,lat,lng,country,iso2,iso3,admin_name,capital,population,id\n";
static const char row[] = "Tokyo,Tokyo,1,2,Japan,JP,JPN,Region,,100,1\n";
static int read_failure, close_failure, reads, closes, injected;
ssize_t __real_getline(char **line, size_t *cap, FILE *fp);
ssize_t __real___getdelim(char **line, size_t *cap, int delim, FILE *fp);
int __real_ferror(FILE *fp);
int __real_fclose(FILE *fp);
ssize_t __wrap_getline(char **line, size_t *cap, FILE *fp) {
    if (++reads == read_failure) { injected = 1; errno = EIO; return -1; }
    return __real_getline(line, cap, fp);
}
/* glibc inlines getline as __getdelim under optimization. Cover both paths. */
ssize_t __wrap___getdelim(char **line, size_t *cap, int delim, FILE *fp) {
    if (++reads == read_failure) { injected = 1; errno = EIO; return -1; }
    return __real___getdelim(line, cap, delim, fp);
}
int __wrap_ferror(FILE *fp) { return injected || __real_ferror(fp); }
int __wrap_fclose(FILE *fp) {
    ++closes;
    int result = __real_fclose(fp);
    if (close_failure) { errno = EIO; return EOF; }
    return result;
}

typedef struct { int types[16], calls, fail_at; char last[TEXT_SIZE]; } Collector;
static int collect(const Response *r, void *ctx) {
    Collector *c = ctx;
    assert(c->calls < 16);
    c->types[c->calls++] = r->type;
    snprintf(c->last, sizeof(c->last), "%s", r->text);
    if (c->calls == c->fail_at) { errno = EPIPE; return -1; }
    return 0;
}
static void fixture(const char *path, const char *heading, const char *body) {
    FILE *fp = fopen(path, "w");
    assert(fp);
    assert(fputs(heading, fp) >= 0);
    assert(fputs(body, fp) >= 0);
    assert(__real_fclose(fp) == 0);
}
static void reset(void) { reads = closes = injected = read_failure = close_failure = 0; }
static void expect_error(const char *path, const Request *req, const char *text) {
    Collector c = {0};
    assert(search_dataset(path, req, collect, &c) == -1);
    assert(c.calls == 1 && c.types[0] == RESP_ERROR);
    assert(strstr(c.last, text));
}

int main(void) {
    char line[1024];
    CityRecord record;
    strcpy(line, "\"A, B\",\"A\"\"B\",1,2,Country,CC,CCC,\"\",,10,\r\n");
    assert(parse_csv_line(line, &record) == COL_COUNT);
    assert(!strcmp(record.fields[0], "A, B"));
    assert(!strcmp(record.fields[1], "A\"B"));
    assert(!strcmp(record.fields[7], "") && !strcmp(record.fields[10], ""));
    strcpy(line, ",,,,,,,,,,");
    assert(parse_csv_line(line, &record) == COL_COUNT);
    const char *bad[] = { "", "a,b", ",,,,,,,,,", ",,,,,,,,,,,",
        "\"a,b,c,d,e,f,g,h,i,j,k", "\"a\"x,b,c,d,e,f,g,h,i,j,k",
        "a\"x,b,c,d,e,f,g,h,i,j,k", "a,b,c,d,e,f,g,h,i,j,k\r",
        "a,b,c,d,e,f,g,h,i,j,k\n\n", "\"a\nb\",b,c,d,e,f,g,h,i,j,k" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        strcpy(line, bad[i]);
        assert(parse_csv_line(line, &record) == -1);
        assert(errno == EINVAL);
    }
    assert(parse_csv_line(NULL, &record) == -1);
    char path[] = "/tmp/so-dataset-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && close(fd) == 0);
    Request req = { .operation = OP_CITY, .value = "Tokyo" };
    fixture(path, header, row);
    Collector c = {0};
    assert(search_dataset(path, &req, collect, &c) == 1);
    assert(c.calls == 2 && c.types[0] == RESP_ROW && c.types[1] == RESP_END);
    for (int fail = 1; fail <= 2; ++fail) {
        reset();
        c = (Collector){ .fail_at = fail };
        assert(search_dataset(path, &req, collect, &c) == -1);
        assert(errno == EPIPE && c.calls == fail && closes == 1);
    }
    req.operation = OP_COUNTRY;
    for (int fail = 1; fail <= 2; ++fail) {
        c = (Collector){ .fail_at = fail };
        assert(search_dataset(path, &req, collect, &c) == -1);
        assert(errno == EPIPE && c.calls == fail && c.types[0] == RESP_NOT_FOUND);
    }
    req.operation = OP_CITY;
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        if (!*bad[i]) continue; /* Empty body is a valid header-only dataset. */
        fixture(path, header, bad[i]);
        expect_error(path, &req, strstr(bad[i], "\n\n") ? "linea 3" : "linea 2");
    }
    fixture(path, "wrong,city_ascii,lat,lng,country,iso2,iso3,admin_name,capital,population,id\n", row);
    expect_error(path, &req, "Encabezado");
    fixture(path, "", "");
    expect_error(path, &req, "vacio");
    fixture(path, header, "");
    c = (Collector){0};
    assert(search_dataset(path, &req, collect, &c) == 0);
    assert(c.calls == 2 && c.types[0] == RESP_NOT_FOUND && c.types[1] == RESP_END);
    fixture(path, header, "\"Tokyo, East\",Tokyo,1,2,Japan,JP,JPN,\"A\"\"B\",,100,1\r\n");
    c = (Collector){0};
    assert(search_dataset(path, &req, collect, &c) == 1);
    fixture(path, header, row);
    FILE *fp = fopen(path, "a");
    assert(fp && fwrite("bad\0csv\n", 1, 8, fp) == 8 && __real_fclose(fp) == 0);
    c = (Collector){0};
    assert(search_dataset(path, &req, collect, &c) == -1);
    assert(c.calls == 2 && c.types[1] == RESP_ERROR && strstr(c.last, "linea 3"));
    fixture(path, header, row);
    for (int stage = 0; stage < 3; ++stage) {
        reset();
        read_failure = stage == 0 ? 1 : stage == 1 ? 3 : 0;
        close_failure = stage == 2;
        c = (Collector){0};
        assert(search_dataset(path, &req, collect, &c) == -1 && errno == EIO);
        assert(c.calls == (stage == 0 ? 1 : 2));
        assert(c.types[c.calls - 1] == RESP_ERROR && closes == 1);
    }
    reset();
    assert(unlink(path) == 0);
    expect_error(path, &req, "abrir");
    c = (Collector){ .fail_at = 1 };
    assert(search_dataset(path, &req, collect, &c) == -1);
    assert(errno == EPIPE && c.calls == 1 && c.types[0] == RESP_ERROR);
    req.operation = 99;
    expect_error(path, &req, "Solicitud");
    req.operation = OP_CITY;
    memset(req.value, 'x', sizeof(req.value));
    expect_error(path, &req, "Solicitud");
    puts("PASS dataset: CSV estricto, contrato, callbacks, errores de lectura/cierre.");
    return 0;
}
