#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "app.h"

/* obs-websocket v5, JSON mode. Uses WinHTTP WebSocket; no Python runtime needed. */
typedef struct {HINTERNET session,connection,socket;} Socket;
static volatile LONG requested_stop=0;
static HINTERNET active_socket=NULL;
static BOOL WINAPI ctrl_handler(DWORD code){
    if(code==CTRL_C_EVENT||code==CTRL_BREAK_EVENT||code==CTRL_CLOSE_EVENT){
        InterlockedExchange(&requested_stop,1);
        /* Shutdown the blocking receiver on a separate console control thread. */
        if(active_socket)WinHttpWebSocketClose(active_socket,1000,NULL,0);
        return TRUE;
    }
    return FALSE;
}
static void ws_close(Socket *c){
    active_socket=NULL;
    if(c->socket)WinHttpCloseHandle(c->socket);
    if(c->connection)WinHttpCloseHandle(c->connection);
    if(c->session)WinHttpCloseHandle(c->session);
    memset(c,0,sizeof(*c));
}
static int ws_connect(Socket*c,const Config *cfg){
    memset(c,0,sizeof(*c));
    wchar_t host[256];app_utf8_to_wide(cfg->obs_host,host,256);
    c->session=WinHttpOpen(L"BST-C/1.0",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!c->session)return 0;
    /* With event subscription, idle streams must not be closed by a 10-second receive timeout. */
    WinHttpSetTimeouts(c->session,5000,5000,5000,30*60*1000);
    c->connection=WinHttpConnect(c->session,host,(INTERNET_PORT)cfg->obs_port,0);
    if(!c->connection)return 0;
    HINTERNET request=WinHttpOpenRequest(c->connection,L"GET",L"/",NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);
    if(!request)return 0;
    int ok=WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0)&&
           WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&
           WinHttpReceiveResponse(request,NULL);
    if(ok)c->socket=WinHttpWebSocketCompleteUpgrade(request,0);
    DWORD ec=GetLastError();WinHttpCloseHandle(request);
    if(!c->socket){app_log("[OBS] WebSocket Upgrade失敗 (WinHTTP %lu)",(unsigned long)ec);return 0;}
    active_socket=c->socket;return 1;
}
static int send_text(Socket *c,const char *s){
    DWORD e=WinHttpWebSocketSend(c->socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,(PVOID)s,(DWORD)strlen(s));
    if(e!=NO_ERROR){app_log("[OBS] WebSocket送信失敗 %lu",(unsigned long)e);return 0;}return 1;
}
static int recv_text(Socket *c,char*out,size_t cap){
    size_t n=0;
    while(!requested_stop){
        DWORD read=0;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        if(n>=cap-1)return 0;
        DWORD e=WinHttpWebSocketReceive(c->socket,out+n,(DWORD)(cap-n-1),&read,&type);
        if(e!=NO_ERROR){app_log("[OBS] WebSocket受信エラー %lu",(unsigned long)e);return 0;}
        n+=read;
        if(type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE){
            USHORT status=0;DWORD len=0;
            WinHttpWebSocketQueryCloseStatus(c->socket,&status,NULL,0,&len);
            app_log("[OBS] ソケットが閉じられました (code=%u)",status);return 0;
        }
        if(type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE){out[n]=0;return 1;}
        if(type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)continue;
        /* Binary protocol isn't used; skip message types we don't understand. */
        if(type==WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE){n=0;continue;}
        if(type==WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE){n=0;continue;}
    }
    return 0;
}
static int sha256_b64(const char*src,char*out,size_t cap){
    BYTE hash[32];DWORD len=sizeof(hash);BCRYPT_ALG_HANDLE algo=NULL;
    if(BCryptOpenAlgorithmProvider(&algo,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)return 0;
    NTSTATUS status=BCryptHash(algo,NULL,0,(PUCHAR)src,(ULONG)strlen(src),hash,sizeof(hash));
    BCryptCloseAlgorithmProvider(algo,0);if(status<0)return 0;
    DWORD want=0;CryptBinaryToStringA(hash,len,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,NULL,&want);
    if(want>cap)return 0;
    return CryptBinaryToStringA(hash,len,CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,out,&want)!=0;
}
static int identify(Socket*c,const Config*cfg){
    char buf[65536],d[8192],auth[4096];
    if(!recv_text(c,buf,sizeof(buf)))return 0;
    if(!json_get(buf,"op",auth,sizeof(auth))||atoi(auth)!=0||!json_get_object(buf,"d",d,sizeof(d))){
        app_log("[OBS] Helloがありません。OBS WebSocket v5か確認してください");return 0;
    }
    char identify[4096];
    char a[2048];
    if(json_get_object(d,"authentication",auth,sizeof(auth))){
        char challenge[512],salt[512],plain[2048],secret[512],response[512];
        if(!*cfg->obs_password){app_log("[OBS] 認証が必要ですがOBSパスワードが未設定です。設定画面を起動します");return -1;}
        if(!json_get(auth,"challenge",challenge,sizeof(challenge))||
           !json_get(auth,"salt",salt,sizeof(salt))){app_log("[OBS] 認証フィールドが不正です");return 0;}
        snprintf(plain,sizeof(plain),"%s%s",cfg->obs_password,salt);
        if(!sha256_b64(plain,secret,sizeof(secret)))return 0;
        snprintf(plain,sizeof(plain),"%s%s",secret,challenge);
        if(!sha256_b64(plain,response,sizeof(response)))return 0;
        json_escape(response,a,sizeof(a));
        snprintf(identify,sizeof(identify),"{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":64,\"authentication\":\"%s\"}}",a);
    }else snprintf(identify,sizeof(identify),"{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":64}}");
    if(!send_text(c,identify)||!recv_text(c,buf,sizeof(buf)))return 0;
    char op[40];if(!json_get(buf,"op",op,sizeof(op))||atoi(op)!=2){
        app_log("[OBS] Identify拒否 (認証情報・OBSのバージョンを確認してください)");return 0;
    }
    app_log("[OBS] WebSocket認証・接続成功");
    return send_text(c,"{\"op\":6,\"d\":{\"requestType\":\"GetStreamStatus\",\"requestId\":\"initial-status\"}}");
}
static void auto_generate(const Config*c,const Session*s){
    if(!c->auto_generate)return;
    Sleep(2000);char err[512]={0};
    if(!generate_timestamps(c,s,NAN,-1,err,sizeof(err)))app_log("[ERROR] 自動生成失敗: %s。generateで再試行してください",err);
}
static void record_start(const Config*c,int64_t begin,const char*method){
    (void)c;char err[512]={0};Session s;
    if(!session_begin(&s,begin,method,err,sizeof(err)))app_log("[ERROR] 開始記録失敗: %s",err);
}
static void record_stop(const Config*c,int64_t end,const char*method){
    Session s;char err[512]={0};
    if(session_finish(&s,end,method,err,sizeof(err)))auto_generate(c,&s);
    else app_log("[ERROR] 終了記録失敗: %s",err);
}
int obs_monitor(const Config*cfg){
    HANDLE lock=CreateMutexW(NULL,TRUE,L"Local\\BST_C_ObsMonitor");
    if(!lock||GetLastError()==ERROR_ALREADY_EXISTS){
        app_log("[ERROR] 監視プログラムが既に実行中です。二重起動はできません");
        if(lock)CloseHandle(lock);
        return 1;
    }
    Config current=*cfg;
    const Config*c=&current;
    SetConsoleCtrlHandler(ctrl_handler,TRUE);
    int known=-1,retries=0;
    char err[512];Session unclosed;
    int session_state=session_open(&unclosed,err,sizeof(err));
    if(session_state<0)app_log("[WARN] 未完了セッションに問題があります: %s",err);
    app_log("[OBS] %s:%d を監視します (Ctrl+Cで終了)",c->obs_host,c->obs_port);
    while(!requested_stop){
        Socket socket={0};
        int connected=ws_connect(&socket,c);
        int identified=connected?identify(&socket,c):0;
        if(identified==-1){
            ws_close(&socket);
            if(!launch_config_and_wait())break;
            char issue[512];
            if(!config_load(&current,issue,sizeof(issue))||!config_validate(&current,issue,sizeof(issue))){
                app_log("[ERROR] OBS認証設定が未完了です: %s",issue);break;
            }
            continue;
        }
        if(!connected||!identified){
            app_log("[WARN] OBSに接続できません (試行%d)",++retries);ws_close(&socket);
            for (int i = 0; i < 5 && !requested_stop; i++) {
                Sleep(1000);
            }
            continue;
        }
        retries=0;
        char msg[65536],data[8192],nested[8192],value[256];
        while(!requested_stop && recv_text(&socket,msg,sizeof(msg))){
            if(!json_get(msg,"op",value,sizeof(value)))continue;
            int op=atoi(value);
            if(!json_get_object(msg,"d",data,sizeof(data)))continue;
            if(op==7){
                if(!json_get(data,"requestId",value,sizeof(value))||strcmp(value,"initial-status"))continue;
                if(!json_get_object(data,"requestStatus",nested,sizeof(nested)))continue;
                char ok[32];if(!json_get(nested,"result",ok,sizeof(ok))||strcmp(ok,"true")){
                    app_log("[OBS] GetStreamStatus失敗");continue;
                }
                if(!json_get_object(data,"responseData",nested,sizeof(nested)))continue;
                int active=json_get(nested,"outputActive",value,sizeof(value))&&!strcmp(value,"true");
                if(active){
                    int64_t begin=(int64_t)time(NULL);char dur[128];
                    if(json_get(nested,"outputDuration",dur,sizeof(dur))){
                        double ms=strtod(dur,NULL);
                        if(ms>0&&ms<864000000)begin-=(int64_t)llround(ms/1000.0);
                    }
                    record_start(c,begin,"obs_status_duration_estimate");
                }else if(known==1)record_stop(c,(int64_t)time(NULL),"obs_status_reconnect_estimate");
                else if(session_state==1){
                    app_log("[WARN] OBS停止中ですが未完了セッションがあります。終了時刻を確認しstopで手動終了してください");
                }
                known=active;session_state=0;
                app_log("[OBS] 初期状態: %s",active?"配信中":"停止中");
            }else if(op==5){
                if(!json_get(data,"eventType",value,sizeof(value))||strcmp(value,"StreamStateChanged"))continue;
                if(!json_get_object(data,"eventData",nested,sizeof(nested)))continue;
                if(!json_get(nested,"outputState",value,sizeof(value)))continue;
                if(!strcmp(value,"OBS_WEBSOCKET_OUTPUT_STARTED")){
                    if(known!=1)record_start(c,(int64_t)time(NULL),"obs_event");
                    known=1;
                }else if(!strcmp(value,"OBS_WEBSOCKET_OUTPUT_STOPPED")){
                    if(known==1)record_stop(c,(int64_t)time(NULL),"obs_event");
                    known=0;
                }
            }
        }
        ws_close(&socket);
        if(!requested_stop){
            app_log("[WARN] WebSocket切断。5秒後に再接続します (OBS配信状態は次回接続時に確認)");
            for(int i=0;i<5&&!requested_stop;i++)Sleep(1000);
        }
    }
    app_log("[OBS] 監視停止。未完了の配信は保持します");
    ReleaseMutex(lock);
    CloseHandle(lock);
    return 0;
}
#endif
