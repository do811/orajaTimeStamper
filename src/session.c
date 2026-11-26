#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/* UTC civil date conversion: accepts ISO-8601 with an explicit timezone offset. */
static int64_t days_from_civil(int y,unsigned m,unsigned d){
    y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yoe=(unsigned)(y-era*400);
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    unsigned yrc=yoe*365+yoe/4-yoe/100+doy;
    return (int64_t)era*146097+(int64_t)yrc-719468;
}
int session_parse_datetime(const char *s,int64_t *out){
    int y,m,d,hh,mm,ss;if(sscanf(s,"%4d-%2d-%2dT%2d:%2d:%2d",&y,&m,&d,&hh,&mm,&ss)!=6)return 0;
    if(y<1970||y>2400||m<1||m>12||d<1||d>31||hh>23||mm>59||ss>60)return 0;
    int64_t v=days_from_civil(y,(unsigned)m,(unsigned)d)*86400+hh*3600+mm*60+ss;
    const char *p=strchr(s,'T');if(!p)return 0;p+=9;
    if(*p=='.'){p++;while(*p>='0'&&*p<='9')p++;}
    if(*p=='+'||*p=='-'){
        int th=0,tm=0;if(sscanf(p+1,"%2d:%2d",&th,&tm)!=2||th>23||tm>59)return 0;
        v+=(*p=='+'?-1:1)*(th*3600+tm*60);
    }else if(*p!='Z' && *p!=0){return 0;}
    /* For timezone-less timestamps assume local PC timezone. */
    if(*p==0){struct tm local={0};local.tm_year=y-1900;local.tm_mon=m-1;local.tm_mday=d;
        local.tm_hour=hh;local.tm_min=mm;local.tm_sec=ss;local.tm_isdst=-1;
        v=(int64_t)mktime(&local);
    }
    *out=v;return 1;
}
int session_path(const Session*s,char*out,size_t cap){
    if(!*s->id)return 0;
    char rel[256];snprintf(rel,sizeof(rel),"sessions/%s.json",s->id);
    app_path(out,cap,rel);return 1;
}
static int save_session(const Session*s,char*err,size_t ec){
    char dir[BST_PATH],path[BST_PATH];app_path(dir,sizeof(dir),"sessions");app_mkdir(dir);
    session_path(s,path,sizeof(path));
    char start[64],end[64],buf[1500];app_local_iso(s->start,start,sizeof(start));
    app_local_iso(s->end,end,sizeof(end));
    char end_json[128];
    if(s->end)snprintf(end_json,sizeof(end_json),"\"%s\"",end);else snprintf(end_json,sizeof(end_json),"null");
    int n=snprintf(buf,sizeof(buf),
        "{\n  \"schema\": 2,\n  \"start_unix\": %lld,\n  \"end_unix\": %lld,\n"
        "  \"start\": \"%s\",\n  \"end\": %s,\n"
        "  \"start_method\": \"%s\",\n  \"end_method\": \"%s\"\n}\n",
        (long long)s->start,(long long)s->end,start,end_json,
        s->start_method,s->end_method);
    if(n<0||(size_t)n>=sizeof(buf)||!app_write_file(path,buf,(size_t)n)){
        snprintf(err,ec,"セッション保存失敗: %s",path);return 0;
    }
    return 1;
}
int session_load_path(Session*s,const char*path,char*err,size_t ec){
    char *j=NULL;if(!app_read_file(path,&j)){snprintf(err,ec,"セッションを読めません: %s",path);return 0;}
    memset(s,0,sizeof(*s));char value[256];
    if(json_get(j,"start_unix",value,sizeof(value)))s->start=strtoll(value,NULL,10);
    else if(json_get(j,"start",value,sizeof(value))&&!session_parse_datetime(value,&s->start)){
        snprintf(err,ec,"開始時刻が不正です");free(j);return 0;
    }
    if(json_get(j,"end_unix",value,sizeof(value)))s->end=strtoll(value,NULL,10);
    else if(json_get(j,"end",value,sizeof(value))&&strcmp(value,"null")&&strcmp(value,"completed"))
        session_parse_datetime(value,&s->end);
    if (json_get(j, "start_method", value, sizeof(value))) {
        size_t len = strlen(value);
        if (len >= sizeof(s->start_method)) len = sizeof(s->start_method) - 1;
        memcpy(s->start_method, value, len);
        s->start_method[len] = 0;
    }
    if (json_get(j, "end_method", value, sizeof(value))) {
        size_t len = strlen(value);
        if (len >= sizeof(s->end_method)) len = sizeof(s->end_method) - 1;
        memcpy(s->end_method, value, len);
        s->end_method[len] = 0;
    }
    const char *p=strrchr(path,'/');const char *q=strrchr(path,'\\');if(q&&(!p||q>p))p=q;
    p=p?p+1:path;snprintf(s->id,sizeof(s->id),"%s",p);
    char*dot=strrchr(s->id,'.');if(dot)*dot=0;
    free(j);
    if(s->start<=0){snprintf(err,ec,"開始時刻がありません");return 0;}
    return 1;
}
int session_open(Session*s,char*err,size_t ec){
    char path[BST_PATH],*j=NULL,id[128];app_path(path,sizeof(path),"active_session.json");
    if(!app_file_exists(path))return 0;
    if(!app_read_file(path,&j)||!json_get(j,"id",id,sizeof(id))){
        snprintf(err,ec,"active_session.jsonが壊れています");free(j);return -1;
    }
    free(j);Session temp={0};snprintf(temp.id,sizeof(temp.id),"%s",id);
    char sp[BST_PATH];session_path(&temp,sp,sizeof(sp));
    if(!session_load_path(s,sp,err,ec))return -1;
    if(s->end>0){snprintf(err,ec,"完了済みのセッションがactiveになっています。手動確認してください");return -1;}
    return 1;
}
int session_begin(Session*s,int64_t start,const char*method,char*err,size_t ec){
    int status=session_open(s,err,ec);if(status==1)return 1;if(status<0)return 0;
    memset(s,0,sizeof(*s));s->start=start;
    app_session_id(start,s->id,sizeof(s->id));
    char id[128];snprintf(id,sizeof(id),"%s",s->id);
    snprintf(s->id,sizeof(s->id),"%.70s_%lld",id,(long long)start);
    snprintf(s->start_method,sizeof(s->start_method),"%s",method);
    if(!save_session(s,err,ec))return 0;
    char active[BST_PATH],buf[300];app_path(active,sizeof(active),"active_session.json");
    int n=snprintf(buf,sizeof(buf),"{\"id\": \"%s\"}\n",s->id);
    if(!app_write_file(active,buf,(size_t)n)){snprintf(err,ec,"active_session.jsonを保存できません");return 0;}
    app_log("[session] 配信開始: %s (method=%s)",s->id,method);
    return 1;
}
int session_finish(Session*s,int64_t end,const char*method,char*err,size_t ec){
    int status=session_open(s,err,ec);if(status!=1){if(status==0)snprintf(err,ec,"未完了セッションがありません");return 0;}
    if(end<=s->start){snprintf(err,ec,"終了時刻が開始時刻以前です");return 0;}
    s->end=end;snprintf(s->end_method,sizeof(s->end_method),"%s",method);
    if(!save_session(s,err,ec))return 0;
    char active[BST_PATH];app_path(active,sizeof(active),"active_session.json");
#ifdef _WIN32
    wchar_t w[BST_PATH];app_utf8_to_wide(active,w,BST_PATH);DeleteFileW(w);
#else
    unlink(active);
#endif
    {char last[BST_PATH],b[300];app_path(last,sizeof(last),"last_session.json");
     int len=snprintf(b,sizeof(b),"{\"id\": \"%s\"}\n",s->id);
     if(len>0)app_write_file(last,b,(size_t)len);}
    app_log("[session] 配信終了: %s (method=%s)",s->id,method);
    return 1;
}
int session_latest(Session*s,char*err,size_t ec){
    /* last_session.json is written when a session finishes. */
    char path[BST_PATH],*j=NULL,id[128];app_path(path,sizeof(path),"last_session.json");
    if(!app_read_file(path,&j)||!json_get(j,"id",id,sizeof(id))){
        free(j);snprintf(err,ec,"終了済みセッションがありません。--sessionで指定してください");return 0;
    }
    free(j);Session dummy={0};snprintf(dummy.id,sizeof(dummy.id),"%s",id);
    char sp[BST_PATH];session_path(&dummy,sp,sizeof(sp));return session_load_path(s,sp,err,ec);
}
