using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Web;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class App {
    internal static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = Int32.MaxValue };
    internal static string FilePath(string file) { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory,file); }
    internal static T Read<T>(string path) { return Json.Deserialize<T>(File.ReadAllText(path)); }
    internal static void Store(string path,object value) {
        string tmp = path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {
            File.WriteAllText(tmp,Json.Serialize(value),new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(tmp,path,null);
            else File.Move(tmp,path);
        } finally { if (File.Exists(tmp)) File.Delete(tmp); }
    }
    internal static FlowLayoutPanel Panel(Form form) {
        var p = new FlowLayoutPanel { Dock=DockStyle.Fill, FlowDirection=FlowDirection.TopDown,
            WrapContents=false, AutoScroll=true, Padding=new Padding(12) };
        form.StartPosition=FormStartPosition.CenterScreen; form.Font=SystemFonts.MessageBoxFont;
        form.Controls.Add(p); return p;
    }
    internal static FlowLayoutPanel Row(Control parent) {
        var p = new FlowLayoutPanel{AutoSize=true,WrapContents=false};
        parent.Controls.Add(p); return p;
    }
    internal static void Label(Control parent,string text) {
        parent.Controls.Add(new Label{Text=text,AutoSize=true,Margin=new Padding(3,8,3,3)});
    }
    internal static Button Button(Control parent,string text,Action action) {
        var b=new Button{Text=text,AutoSize=true}; b.Click += delegate { action(); };
        parent.Controls.Add(b); return b;
    }
    [STAThread] static void Main(string[] args) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        try { Application.Run(args.Contains("--settings") ? (Form)new SettingsWindow() : new RegenerateWindow()); }
        catch(Exception ex) { MessageBox.Show(ex.Message,"orajaTimeStamper"); }
    }
}

internal sealed class Broadcast {
    public long start_unix { get; set; }
    public long end_unix { get; set; }
    public string start { get; set; }
    public string end { get; set; }
    public string start_method { get; set; }
    public string end_method { get; set; }
    internal string path;
    internal static readonly DateTimeOffset Epoch = new DateTimeOffset(1970,1,1,0,0,0,TimeSpan.Zero);
    internal static long Seconds(string text) {
        DateTimeOffset dt;
        return DateTimeOffset.TryParse(text,CultureInfo.InvariantCulture,DateTimeStyles.AllowWhiteSpaces,out dt)
             ? (long)(dt-Epoch).TotalSeconds : 0;
    }
    public override string ToString() {
        return Epoch.AddSeconds(start_unix).ToLocalTime().ToString("yyyy/MM/dd HH:mm:ss")+" ～ "+
               Epoch.AddSeconds(end_unix).ToLocalTime().ToString("yyyy/MM/dd HH:mm:ss");
    }
}
internal sealed class LiveDetails {
    public string actualStartTime {get;set;}
    public string actualEndTime {get;set;}
}
internal sealed class Video { public LiveDetails liveStreamingDetails {get;set;} }
internal sealed class Videos { public Video[] items {get;set;} }

internal sealed class RegenerateWindow : Form {
    readonly ComboBox history=new ComboBox();
    readonly TextBox url=new TextBox(), key=new TextBox();
    readonly string sessions=App.FilePath("sessions");
    readonly Label status=new Label{AutoSize=true};
    readonly FlowLayoutPanel panel;
    public RegenerateWindow() {
        Text="orajaTimeStamper  再生成"; Width=620; Height=405;
        Directory.CreateDirectory(sessions);
        panel=App.Panel(this);
        App.Label(panel,"配信履歴（開始 ～ 終了・ローカル時刻）");
        history.DropDownStyle=ComboBoxStyle.DropDownList; history.Width=550; panel.Controls.Add(history);
        var buttons=App.Row(panel);
        App.Button(buttons,"選択した配信を再生成",delegate { Run(false); });
        App.Button(buttons,"履歴を更新",LoadHistory);
        App.Label(panel,"YouTube の終了済みライブ配信 URL");
        url.Width=550; panel.Controls.Add(url);
        App.Label(panel,"YouTube Data API v3 キー（任意・空欄なら公開ページから取得）");
        key.Width=550; key.UseSystemPasswordChar=true;
        key.Text=Environment.GetEnvironmentVariable("YOUTUBE_API_KEY") ?? ""; panel.Controls.Add(key);
        App.Button(panel,"URL から取得して生成",delegate { Run(true); });
        App.Button(panel,"出力フォルダーを開く",delegate {
            string dir=App.FilePath("timestamps"); Directory.CreateDirectory(dir);
            Process.Start("explorer.exe","\""+dir+"\"");
        });
        panel.Controls.Add(status); LoadHistory();
    }
    void LoadHistory() {
        var list=new List<Broadcast>();
        foreach (string file in Directory.GetFiles(sessions,"*.json")) {
            try {
                var b=App.Read<Broadcast>(file);
                if (b.start_unix==0) b.start_unix=Broadcast.Seconds(b.start);
                if (b.end_unix==0) b.end_unix=Broadcast.Seconds(b.end);
                if (b.start_unix>0 && b.end_unix>b.start_unix) { b.path=file; list.Add(b); }
            } catch (Exception) { continue; } // Incomplete or unreadable sessions cannot be regenerated.
        }
        history.DataSource=list.OrderByDescending(b=>b.start_unix).ToList();
    }
    internal static string VideoId(string input) {
        Uri u;
        if (!Uri.TryCreate(input.Trim(),UriKind.Absolute,out u) ||
            (u.Scheme!="https" && u.Scheme!="http")) throw new ArgumentException("YouTube URL を入力してください。");
        string[] parts=u.AbsolutePath.Trim('/').Split('/');
        string id=null;
        if (u.Host=="youtu.be" && parts.Length==1) id=parts[0];
        if (new[]{"youtube.com","www.youtube.com","m.youtube.com"}.Contains(u.Host)) {
            if (parts.Length==2 && parts[0]=="live") id=parts[1];
            if (parts.Length==1 && parts[0]=="watch") id=HttpUtility.ParseQueryString(u.Query)["v"];
        }
        if (id==null || !Regex.IsMatch(id,@"\A[A-Za-z0-9_-]{11}\z")) throw new ArgumentException("動画 ID を読み取れません。");
        return id;
    }
    internal static Broadcast ParseWatchHead(string html,string expectedId) {
        string canonical=null, start=null, end=null, isLive=null;
        foreach (Match tag in Regex.Matches(html,@"<(meta|link)\b[^>]*>",RegexOptions.IgnoreCase)) {
            var attrs=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            foreach (Match attr in Regex.Matches(tag.Value,@"([\w:-]+)\s*=\s*([""'])(.*?)\2",RegexOptions.Singleline))
                attrs[attr.Groups[1].Value]=WebUtility.HtmlDecode(attr.Groups[3].Value);
            string rel,href,prop,content;
            if (attrs.TryGetValue("rel",out rel) && rel=="canonical" && attrs.TryGetValue("href",out href)) canonical=href;
            if (!attrs.TryGetValue("itemprop",out prop) || !attrs.TryGetValue("content",out content)) continue;
            if (prop=="isLiveBroadcast") isLive=content;
            if (prop=="startDate") start=content;
            if (prop=="endDate") end=content;
        }
        if (canonical==null || VideoId(canonical)!=expectedId || !String.Equals(isLive,"True",StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("この公開ページからライブ配信を確認できません。必要に応じて API キーを指定してください。");
        var b=new Broadcast{start_unix=Broadcast.Seconds(start),end_unix=Broadcast.Seconds(end),
                            start_method="youtube",end_method="youtube"};
        if (b.start_unix<=0 || b.end_unix<=b.start_unix || Broadcast.Epoch.AddSeconds(b.end_unix)>DateTimeOffset.UtcNow)
            throw new InvalidDataException("公開ページで過去の開始・終了を確認できません。配信終了後に試すか、API キーを指定してください。");
        return b;
    }
    static Broadcast PublicTimes(string id) {
        var req=(HttpWebRequest)WebRequest.Create("https://www.youtube.com/watch?v="+id);
        req.Timeout=20000; req.ReadWriteTimeout=20000;
        req.UserAgent="Mozilla/5.0 (Windows NT 10.0; Win64; x64) orajaTimeStamper/1.0";
        req.AutomaticDecompression=DecompressionMethods.GZip | DecompressionMethods.Deflate;
        using (var res=req.GetResponse())
        using (var reader=new StreamReader(res.GetResponseStream())) {
            var head=new StringBuilder();
            char[] chunk=new char[8192]; int count;
            while (head.Length<4*1024*1024 && (count=reader.Read(chunk,0,chunk.Length))>0) {
                int from=Math.Max(0,head.Length-7);
                head.Append(chunk,0,count);
                string tail=head.ToString(from,head.Length-from);
                int close=tail.IndexOf("</head>",StringComparison.OrdinalIgnoreCase);
                if (close>=0) return ParseWatchHead(head.ToString(0,from+close),id);
            }
        }
        throw new InvalidDataException("公開ページから配信日時を取得できません。API キーを指定して再度お試しください。");
    }
    static Broadcast Lookup(string id,string apiKey) {
        ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
        if (String.IsNullOrWhiteSpace(apiKey)) return PublicTimes(id);
        var request=(HttpWebRequest)WebRequest.Create("https://www.googleapis.com/youtube/v3/videos?part=liveStreamingDetails&id="+id);
        request.Timeout=20000; request.ReadWriteTimeout=20000; request.AllowAutoRedirect=false;
        request.Headers["X-Goog-Api-Key"]=apiKey.Trim();
        Videos data;
        try {
            using (var response=request.GetResponse())
            using (var reader=new StreamReader(response.GetResponseStream())) data=App.Json.Deserialize<Videos>(reader.ReadToEnd());
        } catch(WebException ex) {
            using (var res=ex.Response as HttpWebResponse) {
                throw new InvalidOperationException("YouTube API に接続できません。キー・割当・ネットワークを確認してください。 "+
                                                    (res==null ? ex.Status.ToString() : ((int)res.StatusCode).ToString()));
            }
        }
        var details=data.items==null || data.items.Length==0 ? null : data.items[0].liveStreamingDetails;
        if (details==null) throw new InvalidDataException("参照可能なライブ配信情報がありません。");
        var b=new Broadcast{start_unix=Broadcast.Seconds(details.actualStartTime),end_unix=Broadcast.Seconds(details.actualEndTime),
                            start_method="youtube",end_method="youtube"};
        if (b.start_unix<=0 || b.end_unix<=b.start_unix)
            throw new InvalidDataException("実際の開始・終了時刻が確定していません。配信終了後に再度お試しください。");
        return b;
    }
    static string Generate(string file) {
        var psi=new ProcessStartInfo(App.FilePath("timestamp.exe"),"generate --session \""+file+"\"") {
            UseShellExecute=false, CreateNoWindow=true,RedirectStandardOutput=true, RedirectStandardError=true,
            StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8 };
        using (var p=Process.Start(psi)) {
            var stdout=p.StandardOutput.ReadToEndAsync(); var stderr=p.StandardError.ReadToEndAsync();
            p.WaitForExit(); Task.WaitAll(stdout,stderr);
            if (p.ExitCode!=0) throw new InvalidOperationException((stderr.Result+"\n"+stdout.Result).Trim());
            return stdout.Result.Trim();
        }
    }
    async void Run(bool fromUrl) {
        panel.Enabled=false;
        try {
            var b=history.SelectedItem as Broadcast;
            if (fromUrl) {
                string id=VideoId(url.Text), token=key.Text;
                status.Text="YouTube から時刻を取得中…";
                b=await Task.Run(()=>Lookup(id,token));
                if (MessageBox.Show(this,b+"\n配信時間 "+TimeSpan.FromSeconds(b.end_unix-b.start_unix)+
                    "\nこの範囲で生成しますか？","配信時刻の確認",MessageBoxButtons.OKCancel)!=DialogResult.OK) return;
                b.path=Path.Combine(sessions,"youtube_"+id+".json");
                App.Store(b.path,b); LoadHistory();
            }
            if (b==null) throw new InvalidOperationException("終了済みの配信履歴を選んでください。");
            string file=b.path;
            status.Text="生成中…";
            string result=await Task.Run(()=>Generate(file));
            MessageBox.Show(this,result,"生成完了");
        } catch(Exception ex) { MessageBox.Show(this,ex.Message,"再生成"); }
        finally { panel.Enabled=true; status.Text=""; }
    }
}
