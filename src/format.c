#include "format.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

void format_close(OutputFormat *fmt) {
    sqlite3_finalize(fmt->rule);
    sqlite3_finalize(fmt->levels);
    sqlite3_close(fmt->db);
}

static int import_values(sqlite3 *db, const char *sql, const char *json) {
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(st, 1, json, -1, SQLITE_STATIC);
    if (rc == SQLITE_OK) rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

int format_open(OutputFormat *fmt, const Config *cfg, char *err, size_t cap) {
    char path[BST_PATH], *json = NULL;
    app_path(path, sizeof(path), "output-format.json");
    if (sqlite3_open(":memory:", &fmt->db) != SQLITE_OK) goto failed;
    if (sqlite3_exec(fmt->db,
        "CREATE TABLE rules(ch TEXT PRIMARY KEY CHECK(length(ch)=1),replacement TEXT NOT NULL);"
        "CREATE TABLE levels(hash TEXT COLLATE NOCASE,label TEXT,PRIMARY KEY(hash,label));",
        NULL, NULL, NULL) != SQLITE_OK) goto failed;

    if (app_file_exists(path)) {
        if (!app_read_file_limit(path, &json, 256L*1024*1024)) goto failed;
        if (!import_values(fmt->db,
            "INSERT INTO rules SELECT value->>'$.target',value->>'$.replacement' "
            "FROM json_each(?1,'$.rules')", json)) goto failed;
        if (!import_values(fmt->db,
            "INSERT OR IGNORE INTO levels SELECT h.key,l.value FROM json_each(?1,'$.labels') h,"
            "json_each(h.value) l WHERE json_extract(?1,'$.difficulty')=1", json)) goto failed;
    } else if (*cfg->replace_target) {
        char *sql = sqlite3_mprintf("INSERT INTO rules VALUES(%Q,%Q)", cfg->replace_target, cfg->replace_text);
        int ok = sql && sqlite3_exec(fmt->db, sql, NULL, NULL, NULL) == SQLITE_OK;
        sqlite3_free(sql);
        if (!ok) goto failed;
    }
    free(json);
    json = NULL;
    if (sqlite3_prepare_v2(fmt->db, "SELECT replacement FROM rules WHERE ch=?", -1,
                           &fmt->rule, NULL) != SQLITE_OK) goto failed;
    if (sqlite3_prepare_v2(fmt->db,
        "SELECT DISTINCT label FROM levels WHERE hash IN (?,?) ORDER BY label",
        -1, &fmt->levels, NULL) != SQLITE_OK) goto failed;
    return 1;
failed:
    snprintf(err, cap, "出力設定の読み込みに失敗: %s", fmt->db ? sqlite3_errmsg(fmt->db) : path);
    free(json);
    format_close(fmt);
    return 0;
}

char *format_label(OutputFormat *fmt, const char *title, const char *sha, const char *md5) {
    sqlite3_str *result = sqlite3_str_new(fmt->db);
    sqlite3_stmt *levels = fmt->levels, *rule = fmt->rule;
    sqlite3_reset(levels);
    sqlite3_bind_text(levels, 1, sha, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(levels, 2, md5, -1, SQLITE_TRANSIENT);
    int rc;
    while ((rc = sqlite3_step(levels)) == SQLITE_ROW) {
        sqlite3_str_appendf(result, "%s ", sqlite3_column_text(levels, 0));
    }
    if (rc != SQLITE_DONE) goto failed;
    for (const char *p = title; *p;) {
        const char *end = p + 1;
        while (((unsigned char)*end & 0xc0) == 0x80) ++end;
        int len = (int)(end - p);
        sqlite3_reset(rule);
        sqlite3_bind_text(rule, 1, p, len, SQLITE_TRANSIENT);
        rc = sqlite3_step(rule);
        if (rc == SQLITE_ROW) sqlite3_str_appendall(result, (const char *)sqlite3_column_text(rule, 0));
        else if (rc == SQLITE_DONE) sqlite3_str_append(result, p, len);
        else goto failed;
        p = end;
    }
    /* An all-deletion rule can produce a valid empty label. */
    if (!sqlite3_str_length(result) && sqlite3_str_errcode(result) == SQLITE_OK) {
        sqlite3_free(sqlite3_str_finish(result));
        return sqlite3_mprintf("%s", "");
    }
    return sqlite3_str_finish(result);
failed:
    sqlite3_free(sqlite3_str_finish(result));
    return NULL;
}
