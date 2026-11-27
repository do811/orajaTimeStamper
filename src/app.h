#ifndef BST_APP_H
#define BST_APP_H
#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define BST_PATH 2048
#define BST_TEXT 1024

typedef struct {
    char song_db[BST_PATH];
    char score_db[BST_PATH];
    char scoredatalog_db[BST_PATH];
    char obs_host[256];
    char obs_password[512];
    int obs_port;
    int estimate_song_start;
    int auto_generate;
    int include_artist;
    double offset_seconds;
    char replace_target[5];     /* One UTF-8 code point plus the terminator. */
    char replace_text[BST_TEXT];
} Config;

void app_root(char *out, size_t cap);
void app_path(char *out, size_t cap, const char *relative);
void app_log(const char *fmt, ...);
void app_mkdir(const char *path);
int app_read_file_limit(const char *path, char **out, long limit);
int app_read_file(const char *path, char **out);
int app_write_file(const char *path, const char *buf, size_t len);
int app_file_exists(const char *path);
void app_local_iso(int64_t unix_time, char *out, size_t cap);
void app_session_id(int64_t unix_time, char *out, size_t cap);
void app_chapter(double seconds, char *out, size_t cap);
int app_parse_offset(const char *text, double *seconds);
void app_utf8_to_wide(const char *src, wchar_t *dst, size_t cap);
void app_wide_to_utf8(const wchar_t *src, char *dst, size_t cap);

void config_defaults(Config *c);
int config_load(Config *c, char *err, size_t errcap);
int config_save(const Config *c, char *err, size_t errcap);
int config_validate(const Config *c, char *err, size_t errcap);

/* Simple bounded JSON parser returning a field value from a JSON object.
   It handles nested objects, quoted strings and escaped characters. */
int json_get(const char *json, const char *key, char *out, size_t cap);
int json_get_object(const char *json, const char *key, char *out, size_t cap);
void json_escape(const char *src, char *out, size_t cap);

/* Session timestamps are Unix seconds (UTC) to avoid daylight saving ambiguity. */
typedef struct {
    int64_t start;
    int64_t end;
    char start_method[80];
    char end_method[80];
    char id[128];
} Session;
int session_open(Session *s, char *err, size_t errcap); /* 1: current; 0: no session */
int session_begin(Session *s, int64_t start, const char *method, char *err, size_t errcap);
int session_finish(Session *s, int64_t end, const char *method, char *err, size_t errcap);
int session_latest(Session *s, char *err, size_t errcap);
int session_load_path(Session *s, const char *path, char *err, size_t errcap);
int session_parse_datetime(const char *iso, int64_t *unix_time);
int session_path(const Session *s, char *out, size_t cap);

int generate_timestamps(const Config *c, const Session *s, double offset_override,
                        int estimate_override, char *err, size_t errcap);

#ifdef _WIN32
int obs_monitor(const Config *c);
int launch_config_and_wait(void);
#endif
#endif
