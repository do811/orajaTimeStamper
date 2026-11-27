#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <wchar.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#include <sys/stat.h>
#define PATH_SEP '/'
#endif

void app_utf8_to_wide(const char *src, wchar_t *dst, size_t cap) {
#ifdef _WIN32
    if (!src || !cap) return;
    if (!MultiByteToWideChar(CP_UTF8,0,src,-1,dst,(int)cap)) dst[0]=0;
#else
    if (!src || !cap) return;
    mbstowcs(dst, src, cap - 1);
    dst[cap - 1] = 0;
#endif
}
void app_wide_to_utf8(const wchar_t *src, char *dst, size_t cap) {
#ifdef _WIN32
    if (!src || !cap) return;
    if (!WideCharToMultiByte(CP_UTF8,0,src,-1,dst,(int)cap,NULL,NULL)) dst[0]=0;
#else
    if (!src || !cap) return;
    wcstombs(dst, src, cap - 1);
    dst[cap - 1] = 0;
#endif
}
void app_root(char *out, size_t cap) {
#ifdef _WIN32
    wchar_t wbuf[4096]; DWORD n=GetModuleFileNameW(NULL,wbuf,4096);
    if (!n || n>=4096) { snprintf(out,cap,"."); return; }
    wchar_t *p=wcsrchr(wbuf,L'\\'); if (p) *p=0;
    app_wide_to_utf8(wbuf,out,cap);
#else
    char buf[4096]; ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
    if (n<=0) { snprintf(out,cap,"."); return; }
    buf[n]=0; char *p=strrchr(buf,'/'); if(p) *p=0;
    snprintf(out,cap,"%s",buf);
#endif
}
void app_path(char *out, size_t cap, const char *relative) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!relative) return;
    char base[BST_PATH];
    app_root(base, sizeof(base));
    size_t prefix_len = strlen(base);
    size_t suffix_len = strlen(relative);
    /* Refuse truncated paths instead of accidentally reading/writing another file. */
    if (prefix_len >= cap || suffix_len >= cap - prefix_len ||
        prefix_len + 1 >= cap - suffix_len) return;
    memcpy(out, base, prefix_len);
    out[prefix_len] = PATH_SEP;
    memcpy(out + prefix_len + 1, relative, suffix_len + 1);
}
void app_mkdir(const char *path) {
#ifdef _WIN32
    wchar_t w[BST_PATH];app_utf8_to_wide(path,w,BST_PATH);CreateDirectoryW(w,NULL);
#else
    mkdir(path,0755);
#endif
}
int app_file_exists(const char *path) {
#ifdef _WIN32
    wchar_t w[BST_PATH];app_utf8_to_wide(path,w,BST_PATH);DWORD a=GetFileAttributesW(w);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
#else
    return access(path,R_OK)==0;
#endif
}
static FILE *app_fopen(const char *name, const char *mode) {
#ifdef _WIN32
    wchar_t w[BST_PATH],m[16]; app_utf8_to_wide(name,w,BST_PATH);app_utf8_to_wide(mode,m,16);
    return _wfopen(w,m);
#else
    return fopen(name,mode);
#endif
}
int app_read_file(const char *path, char **out) {
    return app_read_file_limit(path, out, 8L*1024*1024);
}
int app_read_file_limit(const char *path, char **out, long limit) {
    FILE *f=app_fopen(path,"rb"); if (!f) return 0;
    if (fseek(f,0,SEEK_END)!=0) { fclose(f);return 0; }
    long sz=ftell(f); if(sz<0 || sz>limit) { fclose(f);return 0; }
    rewind(f);char *p=(char*)malloc((size_t)sz+1);if(!p){fclose(f);return 0;}
    size_t nr=fread(p,1,(size_t)sz,f);fclose(f);if(nr!=(size_t)sz){free(p);return 0;}
    p[sz]=0;*out=p;return 1;
}
int app_write_file(const char *path,const char *buf,size_t len) {
    char tmp[BST_PATH];snprintf(tmp,sizeof(tmp),"%s.tmp",path);
    FILE *f=app_fopen(tmp,"wb");if(!f)return 0;
    int ok=fwrite(buf,1,len,f)==len;
    if(fflush(f)!=0)ok=0;
    if(fclose(f)!=0)ok=0;
    if(!ok)return 0;
#ifdef _WIN32
    wchar_t a[BST_PATH],b[BST_PATH];app_utf8_to_wide(tmp,a,BST_PATH);app_utf8_to_wide(path,b,BST_PATH);
    return MoveFileExW(a,b,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return rename(tmp,path)==0;
#endif
}
static void rotate_log_if_needed(const char *path){
    FILE *probe=app_fopen(path,"rb");
    if(!probe)return;
    long bytes=0;
    if(fseek(probe,0,SEEK_END)==0)bytes=ftell(probe);
    fclose(probe);
    if(bytes<2*1024*1024)return;
    for(int n=3;n>=1;n--){
        char prev[BST_PATH+20],next[BST_PATH+20];
        if(n==1)snprintf(prev,sizeof(prev),"%s",path);
        else snprintf(prev,sizeof(prev),"%s.%d",path,n-1);
        snprintf(next,sizeof(next),"%s.%d",path,n);
#ifdef _WIN32
        wchar_t a[BST_PATH+20],b[BST_PATH+20];
        app_utf8_to_wide(prev,a,BST_PATH+20);app_utf8_to_wide(next,b,BST_PATH+20);
        MoveFileExW(a,b,MOVEFILE_REPLACE_EXISTING);
#else
        rename(prev,next);
#endif
    }
}
void app_log(const char *fmt, ...) {
    char buf[4096];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof(buf),fmt,ap);va_end(ap);
    char dir[BST_PATH],path[BST_PATH];app_path(dir,sizeof(dir),"logs");app_mkdir(dir);
    app_path(path,sizeof(path),"logs/timestamp.log");rotate_log_if_needed(path);
    FILE*f=app_fopen(path,"ab");
    if(f){ time_t now=time(NULL);struct tm *t=localtime(&now);
        char stamp[40];if(t)strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",t);else snprintf(stamp,sizeof(stamp),"unknown");
        fprintf(f,"[%s] %s\n",stamp,buf);fclose(f);
    }
    fprintf(stdout,"%s\n",buf);fflush(stdout);
}
void app_local_iso(int64_t unix_time,char*out,size_t cap){
    time_t t=(time_t)unix_time;struct tm *p=localtime(&t);
    if(!p){snprintf(out,cap,"invalid");return;}
    strftime(out,cap,"%Y-%m-%dT%H:%M:%S",p);
}
void app_session_id(int64_t unix_time,char*out,size_t cap){
    time_t t=(time_t)unix_time;struct tm *p=localtime(&t);
    if(!p){snprintf(out,cap,"session_%lld",(long long)unix_time);return;}
    strftime(out,cap,"%Y%m%d_%H%M%S",p);
}
void app_chapter(double seconds,char*out,size_t cap){
    long long x=(long long)floor(fmax(0.0,seconds)+0.5);
    long long h=x/3600,m=(x/60)%60,s=x%60;
    if(h)snprintf(out,cap,"%lld:%02lld:%02lld",h,m,s);
    else snprintf(out,cap,"%lld:%02lld",x/60,s);
}

int app_parse_offset(const char *text,double *seconds){
    if(!text||!*text||!seconds)return 0;
    int sign=1;if(*text=='-'||*text=='+'){if(*text=='-')sign=-1;text++;}
    const char*colon=strchr(text,':');
    if(!colon){char*end=NULL;double v=strtod(text,&end);if(end==text||*end||!isfinite(v))return 0;*seconds=sign*v;return 1;}
    int count=0;long values[3]={0};const char*ptr=text;
    while(*ptr&&count<3){
        char*end=NULL;long v=strtol(ptr,&end,10);
        if(end==ptr||v<0)return 0;
        values[count++]=v;
        if(*end==':'){ptr=end+1;if(!*ptr)return 0;}
        else if(*end==0){ptr=end;break;}
        else return 0;
    }
    if(*ptr||count<2||count>3||values[count-1]>=60||
       (count==3&&values[1]>=60))return 0;
    double x=count==2?values[0]*60.0+values[1]:values[0]*3600.0+values[1]*60.0+values[2];
    *seconds=sign*x;return 1;
}

void config_defaults(Config *c){memset(c,0,sizeof(*c));snprintf(c->obs_host,sizeof(c->obs_host),"localhost");c->obs_port=4455;c->auto_generate=1;c->estimate_song_start=1;}

#ifdef _WIN32
#include <wincrypt.h>
static int dpapi_encrypt(const char *src,char*out,size_t cap){
    if(!*src){*out=0;return 1;}
    DATA_BLOB in={(DWORD)strlen(src),(BYTE*)src},encrypted={0};
    if(!CryptProtectData(&in,L"orajaTimeStamper",NULL,NULL,NULL,0,&encrypted))return 0;
    DWORD need=0;CryptBinaryToStringA(encrypted.pbData,encrypted.cbData,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,NULL,&need);
    int ok=need<=cap && CryptBinaryToStringA(encrypted.pbData,encrypted.cbData,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out,&need);
    LocalFree(encrypted.pbData);return ok;
}
static int dpapi_decrypt(const char *src,char*out,size_t cap){
    if(!*src){*out=0;return 1;}
    DWORD len=0;if(!CryptStringToBinaryA(src,0,CRYPT_STRING_BASE64,NULL,&len,NULL,NULL))return 0;
    BYTE *buf=(BYTE*)malloc(len);if(!buf)return 0;
    int ok=0;
    if(CryptStringToBinaryA(src,0,CRYPT_STRING_BASE64,buf,&len,NULL,NULL)){
        DATA_BLOB in={len,buf},dec={0};
        if(CryptUnprotectData(&in,NULL,NULL,NULL,NULL,0,&dec)){
            if(dec.cbData<cap){memcpy(out,dec.pbData,dec.cbData);out[dec.cbData]=0;ok=1;}
            LocalFree(dec.pbData);
        }
    }
    free(buf);return ok;
}
#endif
int config_load(Config*c,char*err,size_t ec){
    config_defaults(c);char path[BST_PATH];app_path(path,sizeof(path),"config.json");char *j=NULL;
    if(!app_read_file(path,&j)){snprintf(err,ec,"config.json がありません");return 0;}
    char value[BST_PATH];
#define READS(KEY,FIELD) if(json_get(j,KEY,value,sizeof(value)))snprintf(c->FIELD,sizeof(c->FIELD),"%s",value)
    READS("song_db",song_db);READS("score_db",score_db);READS("scoredatalog_db",scoredatalog_db);
    if(json_get(j,"estimate_song_start",value,sizeof(value)))c->estimate_song_start=!strcmp(value,"true");
    if(json_get(j,"auto_generate",value,sizeof(value)))c->auto_generate=!strcmp(value,"true");
    if(json_get(j,"include_artist",value,sizeof(value)))c->include_artist=!strcmp(value,"true");
    if(json_get(j,"offset_seconds",value,sizeof(value)))c->offset_seconds=strtod(value,NULL);
    if (json_get(j, "replace_target", value, sizeof(value))) {
        size_t len = strlen(value);
        if (len >= sizeof(c->replace_target)) {
            free(j);
            snprintf(err, ec, "置換の対象文字は1文字にしてください");
            return 0;
        }
        memcpy(c->replace_target, value, len + 1);
    }
    if (json_get(j, "replace_text", value, sizeof(value))) {
        size_t len = strlen(value);
        if (len >= sizeof(c->replace_text)) {
            free(j);
            snprintf(err, ec, "置き換え後の文字列が長すぎます");
            return 0;
        }
        memcpy(c->replace_text, value, len + 1);
    }
    char obs[4096];if(json_get_object(j,"obs",obs,sizeof(obs))){
        if (json_get(obs, "host", value, sizeof(value))) {
            size_t len = strlen(value);
            if (len >= sizeof(c->obs_host)) {
                free(j);
                snprintf(err, ec, "OBSホスト名が長すぎます");
                return 0;
            }
            memcpy(c->obs_host, value, len + 1);
        }
        if(json_get(obs,"port",value,sizeof(value)))c->obs_port=atoi(value);
#ifdef _WIN32
        if(json_get(obs,"password_dpapi",value,sizeof(value))&&value[0]&&!dpapi_decrypt(value,c->obs_password,sizeof(c->obs_password))){
            free(j);snprintf(err,ec,"OBSパスワードを復号できません。config.exeで再設定してください");return 0;
        }
#else
        if(json_get(obs,"password",value,sizeof(value)))snprintf(c->obs_password,sizeof(c->obs_password),"%s",value);
#endif
    }
    free(j);return 1;
}
static int single_utf8_character(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    size_t len = strlen(text);
    if (len == 1) return p[0] < 0x80;
    if (len < 2 || len > 4) return 0;
    if ((len == 2 && (p[0] < 0xc2 || p[0] > 0xdf)) ||
        (len == 3 && (p[0] < 0xe0 || p[0] > 0xef)) ||
        (len == 4 && (p[0] < 0xf0 || p[0] > 0xf4))) return 0;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xc0) != 0x80) return 0;
    }
    return !(p[0] == 0xe0 && p[1] < 0xa0) &&
           !(p[0] == 0xed && p[1] >= 0xa0) &&
           !(p[0] == 0xf0 && p[1] < 0x90) &&
           !(p[0] == 0xf4 && p[1] >= 0x90);
}
int config_validate(const Config*c,char*err,size_t ec){
    if(!*c->song_db||!app_file_exists(c->song_db)){snprintf(err,ec,"song_db のファイルを指定してください");return 0;}
    if((!*c->scoredatalog_db||!app_file_exists(c->scoredatalog_db))&&
       (!*c->score_db||!app_file_exists(c->score_db))){snprintf(err,ec,"scoredatalog_db または score_db が必要です");return 0;}
    if(!*c->obs_host||c->obs_port<1||c->obs_port>65535){snprintf(err,ec,"OBSのホスト/ポートを確認してください");return 0;}
    if(c->offset_seconds< -86400||c->offset_seconds>86400){snprintf(err,ec,"補正秒数は-86400～86400にしてください");return 0;}
    if (*c->replace_target && (!single_utf8_character(c->replace_target) ||
                               strpbrk(c->replace_target, "\r\n"))) {
        snprintf(err, ec, "置換の対象文字は1文字にしてください");
        return 0;
    }
    if (strpbrk(c->replace_text, "\r\n")) {
        snprintf(err, ec, "置き換え後の文字列に改行は使えません");
        return 0;
    }
    return 1;
}
int config_save(const Config*c,char*err,size_t ec){
    char a[2*BST_PATH],b[2*BST_PATH],d[2*BST_PATH],h[1024],p[2048];
    json_escape(c->song_db,a,sizeof(a));json_escape(c->score_db,b,sizeof(b));json_escape(c->scoredatalog_db,d,sizeof(d));
    json_escape(c->obs_host,h,sizeof(h));
#ifdef _WIN32
    char pw[2048];if(!dpapi_encrypt(c->obs_password,pw,sizeof(pw))){snprintf(err,ec,"OBSパスワードの暗号化に失敗");return 0;}
    json_escape(pw,p,sizeof(p));
#else
    json_escape(c->obs_password,p,sizeof(p));
#endif
    char target[6 * sizeof(c->replace_target) + 1];
    char replacement[6 * sizeof(c->replace_text) + 1];
    json_escape(c->replace_target, target, sizeof(target));
    json_escape(c->replace_text, replacement, sizeof(replacement));
    char body[20000 + sizeof(target) + sizeof(replacement)];int n=snprintf(body,sizeof(body),
        "{\n  \"song_db\": \"%s\",\n  \"score_db\": \"%s\",\n  \"scoredatalog_db\": \"%s\",\n"
        "  \"obs\": {\"host\": \"%s\", \"port\": %d, "
#ifdef _WIN32
        "\"password_dpapi\": \"%s\"},\n"
#else
        "\"password\": \"%s\"},\n"
#endif
        "  \"estimate_song_start\": %s,\n  \"auto_generate\": %s,\n"
        "  \"include_artist\": %s,\n  \"offset_seconds\": %.3f,\n"
        "  \"replace_target\": \"%s\",\n  \"replace_text\": \"%s\"\n}\n",
        a,b,d,h,c->obs_port,p,c->estimate_song_start?"true":"false",c->auto_generate?"true":"false",
        c->include_artist?"true":"false",c->offset_seconds,target,replacement);
    if(n<=0||(size_t)n>=sizeof(body)){snprintf(err,ec,"設定の長さが上限を超えています");return 0;}
    char path[BST_PATH];app_path(path,sizeof(path),"config.json");
    if(!app_write_file(path,body,(size_t)n)){snprintf(err,ec,"config.json を保存できませんでした");return 0;}
    return 1;
}
