#ifndef ORAJA_FORMAT_H
#define ORAJA_FORMAT_H
#include "app.h"
#include <sqlite3.h>

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *rule;
    sqlite3_stmt *levels;
} OutputFormat;

int format_open(OutputFormat *fmt, const Config *cfg, char *err, size_t cap);
char *format_label(OutputFormat *fmt, const char *title, const char *sha, const char *md5);
void format_close(OutputFormat *fmt);
#endif
