#include "app.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ws(const char *s) { while (*s && isspace((unsigned char)*s)) s++; return s; }
static const char *skip_string(const char *p) {
    if (*p++ != '"') return NULL;
    while (*p) { if (*p == '\\' && p[1]) { p += 2; continue; } if (*p++ == '"') return p; }
    return NULL;
}
static const char *skip_value(const char *p) {
    p = ws(p);
    if (*p == '"') return skip_string(p);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p++;
        while (*(p = ws(p)) && *p != close) {
            const char *next = skip_value(p);
            if (!next || next == p) return NULL;
            p = ws(next);
            if (*p == ',' || *p == ':') p++;
            else if (*p != close) return NULL;
        }
        return *p == close ? p + 1 : NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;
    return p;
}
static int copy_unquoted_string(const char *p, const char *end, char *out, size_t cap) {
    size_t n = 0;
    if (cap == 0) return 0;
    for (; p < end && *p; p++) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            switch (*p) {
                case 'n': if (n + 1 < cap) out[n++] = '\n'; break;
                case 'r': if (n + 1 < cap) out[n++] = '\r'; break;
                case 't': if (n + 1 < cap) out[n++] = '\t'; break;
                case '\\': case '/': case '"': if (n + 1 < cap) out[n++] = *p; break;
                case 'u': /* Config UI emits raw UTF-8; JSON unicode escapes for paths aren't supported. */
                    if (p + 4 < end) p += 4;
                    break;
                default: if (n + 1 < cap) out[n++] = *p; break;
            }
        } else if (n + 1 < cap) out[n++] = *p;
    }
    out[n] = 0;
    return 1;
}
static int json_field(const char *json, const char *key, char *out, size_t cap, int object) {
    const char *p = ws(json);
    if (!p || *p++ != '{' || cap == 0) return 0;
    out[0] = 0;
    while (*(p = ws(p)) && *p != '}') {
        if (*p != '"') return 0;
        const char *ks = p + 1, *ke = skip_string(p);
        if (!ke) return 0;
        p = ws(ke);
        if (*p++ != ':') return 0;
        p = ws(p);
        const char *after = skip_value(p);
        if (!after) return 0;
        if ((size_t)(ke - ks - 1) == strlen(key) && memcmp(ks,key,strlen(key)) == 0) {
            if (object) {
                if (*p != '{' || (size_t)(after-p) >= cap) return 0;
                memcpy(out,p,(size_t)(after-p)); out[after-p]=0; return 1;
            }
            if (*p == '"') return copy_unquoted_string(p+1,after-1,out,cap);
            size_t n = (size_t)(after-p);
            if (n >= cap) return 0;
            memcpy(out,p,n); out[n] = 0; return 1;
        }
        p = ws(after);
        if (*p == ',') p++;
        else if (*p != '}') return 0;
    }
    return 0;
}
int json_get(const char *json, const char *key, char *out, size_t cap) { return json_field(json,key,out,cap,0); }
int json_get_object(const char *json, const char *key, char *out, size_t cap) { return json_field(json,key,out,cap,1); }
void json_escape(const char *src, char *out, size_t cap) {
    size_t k=0; if (!cap) return;
    for (; *src && k+7 < cap; src++) {
        unsigned char c=(unsigned char)*src;
        if (c == '"' || c == '\\') { out[k++]='\\'; out[k++]=c; }
        else if (c == '\n') { out[k++]='\\'; out[k++]='n'; }
        else if (c == '\r') { out[k++]='\\'; out[k++]='r'; }
        else if (c == '\t') { out[k++]='\\'; out[k++]='t'; }
        else if (c < 32) { snprintf(out+k,cap-k,"\\u%04x",c); k+=6; }
        else out[k++]=c;
    }
    out[k]=0;
}
