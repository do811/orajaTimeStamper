#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef _WIN32
int launch_config_and_wait(void){
    wchar_t path[4096];DWORD n=GetModuleFileNameW(NULL,path,4096);
    if(n<1||n>=4000)return 0;
    wchar_t*slash=wcsrchr(path,L'\\');if(!slash)return 0;
    wcscpy(slash+1,L"config.exe");
    SHELLEXECUTEINFOW sei={0};sei.cbSize=sizeof(sei);sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb=L"open";sei.lpFile=path;sei.nShow=SW_SHOW;
    if(!ShellExecuteExW(&sei)){app_log("[ERROR] config.exeを起動できません (Win32=%lu)",GetLastError());return 0;}
    WaitForSingleObject(sei.hProcess,INFINITE);
    DWORD exit_code=1;GetExitCodeProcess(sei.hProcess,&exit_code);
    CloseHandle(sei.hProcess);return exit_code==0;
}
#endif
static void help(void){
    puts("orajaTimeStamper\n"
         "  timestamp.exe             OBS監視を開始（設定がなければconfig.exeを起動）\n"
         "  timestamp.exe monitor     上と同じ\n"
         "  timestamp.exe start [--at YYYY-MM-DDTHH:MM:SS+09:00]\n"
         "  timestamp.exe stop [--at YYYY-MM-DDTHH:MM:SS+09:00]\n"
         "  timestamp.exe generate [--session sessions/xxx.json] [--offset 160] [--basis result|estimated_start]\n"
         "  config.exe                設定GUIを開く");
}
static int run(int argc,char**argv){
    const char *cmd=argc<2?"monitor":argv[1];
    if(!strcmp(cmd,"--help")||!strcmp(cmd,"-h")){help();return 0;}
    if(strcmp(cmd,"monitor")&&strcmp(cmd,"start")&&strcmp(cmd,"stop")&&strcmp(cmd,"generate")){
        fprintf(stderr,"不明なコマンド: %s\n",cmd);help();return 1;
    }
    Config c;char err[1024]={0};
    int ok=config_load(&c,err,sizeof(err));
    if(ok)ok=config_validate(&c,err,sizeof(err));
    if(!ok){
        app_log("[config] 設定が不足しています: %s",err);
#ifdef _WIN32
        puts("config.exeを起動します。必要事項を入力して保存してください。");
        if(!launch_config_and_wait()){
            app_log("[ERROR] 設定GUIが完了しませんでした。config.exeを直接起動してください");return 1;
        }
        if(!config_load(&c,err,sizeof(err))||!config_validate(&c,err,sizeof(err))){
            fprintf(stderr,"設定はまだ不完全です: %s\n",err);return 1;
        }
#else
        fprintf(stderr,"設定がありません。config.jsonを作成してください\n");return 1;
#endif
    }
    Session s={0};
    if(!strcmp(cmd,"monitor")){
#ifdef _WIN32
        return obs_monitor(&c);
#else
        puts("OBS監視はWindows版で利用してください");return 1;
#endif
    }
    int64_t at=(int64_t)time(NULL);const char*session_file=NULL;
    double offset=NAN;int basis=-1;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--at")&&i+1<argc){
            if(!session_parse_datetime(argv[++i],&at)){fprintf(stderr,"日時形式が不正です\n");return 1;}
        }else if(!strcmp(argv[i],"--session")&&i+1<argc)session_file=argv[++i];
        else if(!strcmp(argv[i],"--offset")&&i+1<argc){
            if(!app_parse_offset(argv[++i],&offset)){fprintf(stderr,"--offsetの指定が不正です\n");return 1;}
        }
        else if(!strcmp(argv[i],"--basis")&&i+1<argc){
            const char *b=argv[++i];
            if(!strcmp(b,"result"))basis=0;
            else if(!strcmp(b,"estimated_start"))basis=1;
            else{fprintf(stderr,"--basisはresultまたはestimated_startです\n");return 1;}
        }else{fprintf(stderr,"不明なオプション: %s\n",argv[i]);return 1;}
    }
    if(!strcmp(cmd,"start")){
        if(!session_begin(&s,at,"manual",err,sizeof(err))){fprintf(stderr,"ERROR: %s\n",err);return 1;}
    }else if(!strcmp(cmd,"stop")){
        if(!session_finish(&s,at,"manual",err,sizeof(err))){fprintf(stderr,"ERROR: %s\n",err);return 1;}
        if(c.auto_generate&&!generate_timestamps(&c,&s,NAN,-1,err,sizeof(err))){
            app_log("[ERROR] 自動生成失敗: %s",err);return 1;
        }
    }else if(!strcmp(cmd,"generate")){
        if(session_file){
            if(!session_load_path(&s,session_file,err,sizeof(err))){fprintf(stderr,"ERROR: %s\n",err);return 1;}
        }else if(!session_latest(&s,err,sizeof(err))){fprintf(stderr,"ERROR: %s\n",err);return 1;}
        if(!generate_timestamps(&c,&s,offset,basis,err,sizeof(err))){fprintf(stderr,"ERROR: %s\n",err);return 1;}
    }
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t**wargv){
    SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
    char**argv=(char**)calloc((size_t)argc+1,sizeof(char*));if(!argv)return 1;
    for(int i=0;i<argc;i++){argv[i]=(char*)calloc(BST_PATH,1);
        if(!argv[i])return 1;
        app_wide_to_utf8(wargv[i],argv[i],BST_PATH);
    }
    int result=run(argc,argv);
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
    return result;
}
#else
int main(int argc,char**argv){return run(argc,argv);}
#endif
