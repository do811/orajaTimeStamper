#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"

#define WIN_WIDTH 840
#define WIN_HEIGHT 610
#define ID_SONG 101
#define ID_SCORE 102
#define ID_SCORELOG 103
#define ID_HOST 104
#define ID_PORT 105
#define ID_PASSWORD 106
#define ID_OFFSET 107
#define ID_ESTIMATE 108
#define ID_AUTOGEN 109
#define ID_ARTIST 110
#define ID_SAVE 120
#define ID_CANCEL 121
#define ID_BROWSE1 201
#define ID_BROWSE2 202
#define ID_BROWSE3 203
static Config cfg;
static HFONT font;
static HWND fields[11];
static HINSTANCE instance;
static HWND win;
static int saved_this_run=0;
static HWND element(const wchar_t*kind,const wchar_t*text,DWORD style,int x,int y,int w,int h,int id){
    HWND hwnd=CreateWindowExW(0,kind,text,style|WS_CHILD|WS_VISIBLE,x,y,w,h,win,(HMENU)(INT_PTR)id,instance,NULL);
    if(font)SendMessageW(hwnd,WM_SETFONT,(WPARAM)font,TRUE);
    return hwnd;
}
static void edit_set(int id,const char*value){wchar_t w[BST_PATH];app_utf8_to_wide(value,w,BST_PATH);SetWindowTextW(fields[id-100],w);}
static void edit_get(int id,char*out,size_t cap){wchar_t w[BST_PATH];GetWindowTextW(fields[id-100],w,BST_PATH);app_wide_to_utf8(w,out,cap);}
static void browse(int id){
    wchar_t chosen[BST_PATH]={0};GetWindowTextW(fields[id-100],chosen,BST_PATH);
    OPENFILENAMEW of={0};of.lStructSize=sizeof(of);of.hwndOwner=win;
    of.lpstrFile=chosen;of.nMaxFile=BST_PATH;
    of.lpstrFilter=L"SQLite Database (*.db)\0*.db\0すべてのファイル (*.*)\0*.*\0\0";
    of.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(GetOpenFileNameW(&of))SetWindowTextW(fields[id-100],chosen);
}
static void save(){
    edit_get(ID_SONG,cfg.song_db,sizeof(cfg.song_db));
    edit_get(ID_SCORE,cfg.score_db,sizeof(cfg.score_db));
    edit_get(ID_SCORELOG,cfg.scoredatalog_db,sizeof(cfg.scoredatalog_db));
    edit_get(ID_HOST,cfg.obs_host,sizeof(cfg.obs_host));
    edit_get(ID_PASSWORD,cfg.obs_password,sizeof(cfg.obs_password));
    char port[100],offset[100],err[1024]={0};
    edit_get(ID_PORT,port,sizeof(port));edit_get(ID_OFFSET,offset,sizeof(offset));
    cfg.obs_port=atoi(port);
    if(!app_parse_offset(offset,&cfg.offset_seconds)){
        MessageBoxW(win,L"補正時間は秒数または分:秒（例 160 / 2:40）で指定してください。",L"補正時間が不正です",MB_ICONERROR);
        return;
    }
    cfg.estimate_song_start=SendMessageW(fields[ID_ESTIMATE-100],BM_GETCHECK,0,0)==BST_CHECKED;
    cfg.auto_generate=SendMessageW(fields[ID_AUTOGEN-100],BM_GETCHECK,0,0)==BST_CHECKED;
    cfg.include_artist=SendMessageW(fields[ID_ARTIST-100],BM_GETCHECK,0,0)==BST_CHECKED;
    if(!config_validate(&cfg,err,sizeof(err))||!config_save(&cfg,err,sizeof(err))){
        wchar_t w[1024];app_utf8_to_wide(err,w,1024);
        MessageBoxW(win,w,L"保存できません",MB_ICONERROR|MB_OK);return;
    }
    saved_this_run=1;
    MessageBoxW(win,L"config.jsonを保存しました。\nこの画面を閉じて監視を開始できます。",L"保存完了",MB_ICONINFORMATION);
    DestroyWindow(win);
}
static void make_ui(){
    NONCLIENTMETRICSW m={0};m.cbSize=sizeof(m);
    if(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(m),&m,0))
        font=CreateFontIndirectW(&m.lfMessageFont);
    element(L"STATIC",L"beatoraja 配信タイムスタンプ ─ 初期設定",SS_LEFT,22,18,760,30,0);
    element(L"STATIC",L"DBを参照し、OBS WebSocket v5の接続先を設定します。",SS_LEFT,22,54,760,24,0);
    struct {int id;int y;const wchar_t*label;} dbs[]={
        {ID_SONG,105,L"楽曲DB  (songdata.db / song.db)"},
        {ID_SCORE,171,L"スコアDB  (score.db / 任意)"},
        {ID_SCORELOG,237,L"プレー履歴DB  (scoredatalog.db / 優先)"}
    };
    for(int i=0;i<3;i++){
        int id=dbs[i].id,y=dbs[i].y;
        element(L"STATIC",dbs[i].label,SS_LEFT,22,y,500,22,0);
        fields[id-100]=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,22,y+25,682,28,id);
        element(L"BUTTON",L"参照...",BS_PUSHBUTTON|WS_TABSTOP,716,y+25,94,28,ID_BROWSE1+i);
    }
    element(L"STATIC",L"OBS の IPアドレス / ホスト名",SS_LEFT,22,317,290,24,0);
    element(L"STATIC",L"OBS WebSocket ポート",SS_LEFT,330,317,220,24,0);
    fields[ID_HOST-100]=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,22,342,270,28,ID_HOST);
    fields[ID_PORT-100]=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,330,342,140,28,ID_PORT);
    element(L"STATIC",L"OBS WebSocket パスワード（不要な環境は空欄）",SS_LEFT,22,389,600,24,0);
    fields[ID_PASSWORD-100]=element(L"EDIT",L"",WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,22,413,462,28,ID_PASSWORD);
    element(L"STATIC",L"補正（秒 / 分:秒）",SS_LEFT,520,389,250,24,0);
    fields[ID_OFFSET-100]=element(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,520,413,145,28,ID_OFFSET);
    fields[ID_ESTIMATE-100]=element(L"BUTTON",L"曲開始時刻を結果保存時刻と譜面長から推定",BS_AUTOCHECKBOX|WS_TABSTOP,22,458,490,24,ID_ESTIMATE);
    fields[ID_AUTOGEN-100]=element(L"BUTTON",L"配信終了時に自動生成",BS_AUTOCHECKBOX|WS_TABSTOP,22,491,320,24,ID_AUTOGEN);
    fields[ID_ARTIST-100]=element(L"BUTTON",L"曲名にアーティスト名を含める",BS_AUTOCHECKBOX|WS_TABSTOP,365,491,355,24,ID_ARTIST);
    element(L"BUTTON",L"保存",BS_DEFPUSHBUTTON|WS_TABSTOP,576,530,110,34,ID_SAVE);
    element(L"BUTTON",L"キャンセル",BS_PUSHBUTTON|WS_TABSTOP,699,530,110,34,ID_CANCEL);
    edit_set(ID_SONG,cfg.song_db);edit_set(ID_SCORE,cfg.score_db);
    edit_set(ID_SCORELOG,cfg.scoredatalog_db);edit_set(ID_HOST,cfg.obs_host);
    edit_set(ID_PASSWORD,cfg.obs_password);
    char tmp[80];snprintf(tmp,sizeof(tmp),"%d",cfg.obs_port);edit_set(ID_PORT,tmp);
    snprintf(tmp,sizeof(tmp),"%.3f",cfg.offset_seconds);edit_set(ID_OFFSET,tmp);
    SendMessageW(fields[ID_ESTIMATE-100],BM_SETCHECK,cfg.estimate_song_start?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(fields[ID_AUTOGEN-100],BM_SETCHECK,cfg.auto_generate?BST_CHECKED:BST_UNCHECKED,0);
    SendMessageW(fields[ID_ARTIST-100],BM_SETCHECK,cfg.include_artist?BST_CHECKED:BST_UNCHECKED,0);
}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    (void)lp;
    switch(msg){
        case WM_CREATE:win=hwnd;make_ui();return 0;
        case WM_COMMAND:
            switch(LOWORD(wp)){
                case ID_BROWSE1:browse(ID_SONG);return 0;
                case ID_BROWSE2:browse(ID_SCORE);return 0;
                case ID_BROWSE3:browse(ID_SCORELOG);return 0;
                case ID_SAVE:save();return 0;
                case ID_CANCEL:DestroyWindow(hwnd);return 0;
            }
            break;
        case WM_DESTROY:if(font)DeleteObject(font);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE prev,PWSTR command,int show){
    (void)prev;(void)command;(void)show;instance=h;
    char err[1024];if(!config_load(&cfg,err,sizeof(err)))config_defaults(&cfg);
    WNDCLASSW cls={0};cls.lpfnWndProc=wndproc;cls.hInstance=h;cls.lpszClassName=L"BST_Config";
    cls.hCursor=LoadCursor(NULL,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls))return 1;
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Beatoraja Stream Timestamps - config.exe",
          WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE,
          CW_USEDEFAULT,CW_USEDEFAULT,WIN_WIDTH,WIN_HEIGHT,NULL,NULL,h,NULL);
    if(!window)return 1;
    MSG m;while(GetMessageW(&m,NULL,0,0)>0){if(!IsDialogMessageW(window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    /* Return 0 only if settings now exist and are valid. */
    if(!saved_this_run||!config_load(&cfg,err,sizeof(err))||!config_validate(&cfg,err,sizeof(err)))return 1;
    return 0;
}
#endif
