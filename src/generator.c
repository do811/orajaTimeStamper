#include "format.h"
#include <errno.h>
#include <limits.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MISSING_TITLE "[タイトル不明]"
typedef struct {
    double sec;
    int seq;
    char *name;
} Chapter;
static int cmp_chapter(const void*pa,const void*pb){
    const Chapter*a=(const Chapter*)pa,*b=(const Chapter*)pb;
    if (a->sec < b->sec) return -1;
    if (a->sec > b->sec) return 1;
    return (a->seq > b->seq) - (a->seq < b->seq);
}
static sqlite3* open_ro(const char*path,char*err,size_t ec){
    sqlite3 *db=NULL;
    int ret=sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,NULL);
    if(ret!=SQLITE_OK){snprintf(err,ec,"DBを読み込めません (%s): %.180s",path,db?sqlite3_errmsg(db):"unknown");sqlite3_close(db);return NULL;}
    sqlite3_busy_timeout(db,5000);return db;
}
static int has_column(sqlite3*db,const char*table,const char*col){
    char sql[180];snprintf(sql,sizeof(sql),"PRAGMA table_info(\"%s\")",table);
    sqlite3_stmt*st=NULL;int ok=0;
    if(sqlite3_prepare_v2(db,sql,-1,&st,NULL)==SQLITE_OK){
        while(sqlite3_step(st)==SQLITE_ROW){const unsigned char *n=sqlite3_column_text(st,1);
            if(n&&!sqlite3_stricmp((const char*)n,col)){ok=1;break;}}
    }
    sqlite3_finalize(st);return ok;
}
static int find_song(sqlite3_stmt*query,const char*sha,char*label,size_t cap,double*length,int artist){
    *length=-1;sqlite3_reset(query);sqlite3_clear_bindings(query);
    sqlite3_bind_text(query,1,sha,-1,SQLITE_TRANSIENT);
    if(sqlite3_step(query)!=SQLITE_ROW){snprintf(label,cap,"[曲名不明 %.12s]",sha);return 0;}
    const char *title=(const char*)sqlite3_column_text(query,0);
    const char *sub=(const char*)sqlite3_column_text(query,1);
    const char *performer=(const char*)sqlite3_column_text(query,2);
    if(sqlite3_column_type(query,3)!=SQLITE_NULL)*length=sqlite3_column_double(query,3);
    snprintf(label,cap,"%s",title&&*title?title:MISSING_TITLE);
    size_t n=strlen(label);
    if(sub&&*sub&&n+strlen(sub)+3<cap){label[n++]=' ';snprintf(label+n,cap-n,"%s",sub);}
    n=strlen(label);
    if(artist&&performer&&*performer&&n+strlen(performer)+4<cap)
        snprintf(label+n,cap-n," / %s",performer);
    /* YouTube chapters are single lines. */
    for(char*p=label;*p;p++)if(*p=='\n'||*p=='\r')*p=' ';
    return 1;
}
static FILE* utf8_open(const char*path){
#ifdef _WIN32
    wchar_t w[BST_PATH];app_utf8_to_wide(path,w,BST_PATH);return _wfopen(w,L"wb");
#else
    return fopen(path,"wb");
#endif
}
static FILE *new_timestamp(int64_t start, char *path, size_t cap) {
    char date[32], relative[100];
    time_t ts = (time_t)start;
    struct tm *local = localtime(&ts);
    if (!local || !strftime(date,sizeof(date),"%Y%m%d__%H%M",local)) return NULL;
    for (unsigned v = 1; v < UINT_MAX; ++v) {
        snprintf(relative,sizeof(relative),"timestamps/%s_%u.txt",date,v);
        app_path(path,cap,relative);
#ifdef _WIN32
        wchar_t wide[BST_PATH];
        app_utf8_to_wide(path,wide,BST_PATH);
        int fd = _wopen(wide,_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,_S_IREAD | _S_IWRITE);
        if (fd >= 0) {
            FILE *f = _fdopen(fd,"wb");
            if (!f) { _close(fd); _wremove(wide); }
            return f;
        }
#else
        FILE *f = fopen(path,"wbx");
        if (f) return f;
#endif
        if (errno != EEXIST) return NULL;
    }
    return NULL;
}
int generate_timestamps(const Config*c,const Session*s,double offset_override,int estimate_override,char*err,size_t ec){
    if(!s->start||s->end<=s->start){snprintf(err,ec,"完了済み配信セッションが必要です");return 0;}
    const char *score_path=NULL,*table=NULL;
    if(*c->scoredatalog_db&&app_file_exists(c->scoredatalog_db)){
        score_path=c->scoredatalog_db;table="scoredatalog";
    }else if(*c->score_db&&app_file_exists(c->score_db)){
        score_path=c->score_db;table="score";
    }else{snprintf(err,ec,"scoredatalog.db / score.db が見つかりません");return 0;}
    sqlite3 *score=open_ro(score_path,err,ec);if(!score)return 0;
    sqlite3 *songs=open_ro(c->song_db,err,ec);if(!songs){sqlite3_close(score);return 0;}
    if(!has_column(score,table,"sha256")||!has_column(score,table,"date")||
       !has_column(songs,"song","sha256")||!has_column(songs,"song","title")){
        snprintf(err,ec,"DBのテーブル/カラム形式が一致しません (song, %s)",table);
        sqlite3_close(score);sqlite3_close(songs);return 0;
    }
    char select[1000];snprintf(select,sizeof(select),"SELECT sha256,date FROM %s WHERE date>=?1 AND date<=?2 AND sha256 IS NOT NULL ORDER BY date,rowid",table);
    sqlite3_stmt *plays=NULL,*lookup=NULL;
    int ok=sqlite3_prepare_v2(score,select,-1,&plays,NULL)==SQLITE_OK;
    const int subtitle=has_column(songs,"song","subtitle"),artist=has_column(songs,"song","artist"),
        length=has_column(songs,"song","length"),adddate=has_column(songs,"song","adddate");
    snprintf(select,sizeof(select),"SELECT title,%s,%s,%s,%s FROM song WHERE sha256=?1 %s LIMIT 1",
        subtitle?"subtitle":"''",artist?"artist":"''",length?"length":"NULL",has_column(songs,"song","md5")?"md5":"NULL",adddate?"ORDER BY adddate DESC":"");
    if(ok)ok=sqlite3_prepare_v2(songs,select,-1,&lookup,NULL)==SQLITE_OK;
    if(!ok){snprintf(err,ec,"SQLiteクエリを準備できません: %s / %s",sqlite3_errmsg(score),sqlite3_errmsg(songs));
        sqlite3_finalize(plays);sqlite3_finalize(lookup);sqlite3_close(score);sqlite3_close(songs);return 0;}
    sqlite3_bind_int64(plays,1,(sqlite3_int64)s->start);
    sqlite3_bind_int64(plays,2,(sqlite3_int64)s->end);
    int legacy_scorelog=0;
    if(!strcmp(table,"scoredatalog")){
        sqlite3_stmt *st=NULL;
        if(sqlite3_prepare_v2(score,"PRAGMA table_info(scoredatalog)",-1,&st,NULL)==SQLITE_OK){
            while(sqlite3_step(st)==SQLITE_ROW){
                const char *column=(const char*)sqlite3_column_text(st,1);
                if(column&&!sqlite3_stricmp(column,"sha256")&&sqlite3_column_int(st,5)>0)
                    legacy_scorelog=1;
            }
        }
        sqlite3_finalize(st);
    }
    OutputFormat format = {0};
    if (!format_open(&format,c,err,ec)) {
        sqlite3_finalize(plays); sqlite3_finalize(lookup);
        sqlite3_close(songs); sqlite3_close(score);
        return 0;
    }
    int estimated=estimate_override<0?c->estimate_song_start:estimate_override;
    double offset=isnan(offset_override)?c->offset_seconds:offset_override;
    int missing=0,nolength=0,clamped=0,n=0,capacity=0,run_rc=SQLITE_OK;
    Chapter*chapters=NULL;
    while((run_rc=sqlite3_step(plays))==SQLITE_ROW){
        const char *sha=(const char*)sqlite3_column_text(plays,0);if(!sha)continue;
        double played=(double)sqlite3_column_int64(plays,1);
        char name[BST_TEXT];double millis=-1;
        int found = find_song(lookup,sha,name,sizeof(name),&millis,c->include_artist);
        if (!found) missing++;
        double sec=played-(double)s->start;
        if(estimated){if(millis>0&&millis<=3600000.0)sec-=millis/1000.0;else nolength++;}
        sec-=offset;if(sec<0){sec=0;clamped++;}
        if(n==capacity){int cap=capacity?capacity*2:32;Chapter*newc=(Chapter*)realloc(chapters,(size_t)cap*sizeof(*chapters));
            if(!newc){snprintf(err,ec,"メモリ不足");run_rc=SQLITE_NOMEM;break;}chapters=newc;capacity=cap;}
        chapters[n].sec=sec;chapters[n].seq=n;chapters[n].name=format_label(&format,name,sha,found ? (const char *)sqlite3_column_text(lookup,4) : NULL);
        if(!chapters[n].name){snprintf(err,ec,"メモリ不足");run_rc=SQLITE_NOMEM;break;}
        n++;
    }
    format_close(&format);
    if(run_rc!=SQLITE_DONE&&run_rc!=SQLITE_NOMEM)
        snprintf(err,ec,"スコアDB検索エラー: %s",sqlite3_errmsg(score));
    sqlite3_finalize(plays);sqlite3_finalize(lookup);sqlite3_close(songs);sqlite3_close(score);
    if(run_rc!=SQLITE_DONE)goto fail;
    qsort(chapters,(size_t)n,sizeof(*chapters),cmp_chapter);
    char dir[BST_PATH],out[BST_PATH],report[BST_PATH],relative[256];
    if (!*s->id) { snprintf(err, ec, "セッションIDが不正です"); goto fail; }
    snprintf(relative, sizeof(relative), "sessions/%s_report.txt", s->id);
    app_path(report, sizeof(report), relative);
    if (!*report) {
        snprintf(err, ec, "出力パスが長すぎます");
        goto fail;
    }
    app_path(dir, sizeof(dir), "timestamps");
    app_mkdir(dir);
    out[0]=0;
    FILE *f=new_timestamp(s->start,out,sizeof(out));if(!f){snprintf(err,ec,"出力できません: %s",out);goto fail;}
    fputs("\xef\xbb\xbf",f);
    fputs("0:00 配信開始\n",f);
    for(int i=0;i<n;i++){char stamp[64];app_chapter(chapters[i].sec,stamp,sizeof(stamp));
        fprintf(f,"%s ",stamp);
        fputs(chapters[i].name, f);
        fputc('\n', f);
    }
    char endtime[64];app_chapter((double)(s->end-s->start),endtime,sizeof(endtime));
    fprintf(f,"%s 配信終了\n",endtime);
    int writeok=ferror(f)==0;writeok=fclose(f)==0&&writeok;
    if(!writeok){
#ifdef _WIN32
        wchar_t wide[BST_PATH];app_utf8_to_wide(out,wide,BST_PATH);_wremove(wide);
#else
        remove(out);
#endif
        snprintf(err,ec,"タイムスタンプ書き込みに失敗");goto fail;}
    f=utf8_open(report);
    if(f){char startiso[64],endiso[64];app_local_iso(s->start,startiso,sizeof(startiso));app_local_iso(s->end,endiso,sizeof(endiso));
        fputs("\xef\xbb\xbf",f);
        fprintf(f,"開始: %s\n終了: %s\n開始取得: %s\n終了取得: %s\n使用DB: %s\n対象プレー: %d件\n曲開始推定: %s\n前倒し補正: %.3f秒\n\n",
            startiso,endiso,s->start_method,s->end_method,score_path,n,estimated?"ON":"OFF",offset);
        if(!strcmp(table,"score"))fputs("注意: score.dbは譜面ごとの最終結果しか保存しないため、同じ譜面の再プレーを復元できません。\n",f);
        if(estimated)fputs("注意: 曲開始は結果保存時刻 - 譜面長の推定です。選曲決定時刻ではありません。\n",f);
        if(legacy_scorelog)fputs("注意: 古いscoredatalogのsha256が主キーのため、複数回プレー履歴が欠けている可能性があります。\n",f);
        if(missing)fprintf(f,"注意: 曲名不明 %d件\n",missing);
        if(nolength)fprintf(f,"注意: 曲長不明 %d件\n",nolength);
        if(clamped)fprintf(f,"注意: 0:00より前の推定 %d件を丸めました\n",clamped);
        if(strstr(s->start_method,"estimate"))fputs("注意: OBS配信時間から開始時刻を推定しています。\n",f);
        if(strstr(s->end_method,"poll"))fputs("注意: 終了時刻はOBS状態を検出した時刻であり、最大で確認間隔の遅延があります。\n",f);
        fclose(f);
    }
    app_log("[generate] %d曲を書き出しました: %s",n,out);
    if(!strcmp(table,"score"))app_log("[warning] score.dbは複数回プレーの完全な履歴を持ちません");
    for (int i = 0; i < n; i++) {
        sqlite3_free(chapters[i].name);
    }
    free(chapters);
    return 1;
fail:
    for (int i = 0; i < n; i++) {
        sqlite3_free(chapters[i].name);
    }
    free(chapters);
    return 0;
}
