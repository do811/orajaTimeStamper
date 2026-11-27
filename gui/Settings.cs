using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;

internal sealed class Rule {
    public string target { get; set; }
    public string replacement { get; set; }
}
internal sealed class OutputSettings {
    public List<Rule> rules { get; set; }
    public bool difficulty { get; set; }
    public string folder { get; set; }
    public Dictionary<string,List<string>> labels { get; set; }
}
internal sealed class CachedTable {
    public TableFolder[] folder { get; set; }
}
internal sealed class TableFolder {
    public string name { get; set; }
    public TableChart[] songs { get; set; }
}
internal sealed class TableChart {
    public string md5 { get; set; }
    public string sha256 { get; set; }
}

internal sealed class SettingsWindow : Form {
    readonly DataGridView rules = new DataGridView();
    readonly CheckBox difficulty = new CheckBox();
    readonly TextBox folder = new TextBox();
    readonly string path = App.FilePath("output-format.json");

    public SettingsWindow() {
        Text = "orajaTimeStamper  出力設定";
        Width = 620; Height = 440;
        var panel = App.Panel(this);
        rules.Width = 552; rules.Height = 205;
        rules.AllowUserToAddRows = false;
        rules.Columns.Add("target","対象文字（1文字）");
        rules.Columns.Add("replacement","置き換え後（空欄で削除）");
        rules.Columns[0].Width = 155;
        rules.Columns[1].AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
        panel.Controls.Add(rules);
        var buttons = App.Row(panel);
        App.Button(buttons, "＋ 行を追加", delegate {
            int row = rules.Rows.Add();
            rules.CurrentCell = rules.Rows[row].Cells[0]; rules.BeginEdit(true);
        });
        App.Button(buttons, "選択行を削除", delegate {
            if (rules.CurrentRow != null) rules.Rows.RemoveAt(rules.CurrentRow.Index);
        });
        difficulty.Text = "一致する譜面に難易度表の記号・レベルを表示"; difficulty.AutoSize = true;
        panel.Controls.Add(difficulty);
        App.Label(panel,"beatoraja の table フォルダー（保存時に難易度表を読み込み）");
        var picker = App.Row(panel);
        folder.Width = 435; picker.Controls.Add(folder);
        App.Button(picker,"参照…", delegate {
            using (var dlg = new FolderBrowserDialog()) {
                if (Directory.Exists(folder.Text)) dlg.SelectedPath = folder.Text;
                if (dlg.ShowDialog(this) == DialogResult.OK) folder.Text = dlg.SelectedPath;
            }
        });
        App.Button(panel,"出力設定を保存", Save);
        OutputSettings saved = File.Exists(path) ? App.Read<OutputSettings>(path) : null;
        if (saved != null) {
            difficulty.Checked = saved.difficulty; folder.Text = saved.folder;
            foreach (var r in saved.rules) rules.Rows.Add(r.target,r.replacement);
        } else if (File.Exists(App.FilePath("config.json"))) {
            var cfg = App.Read<Dictionary<string,object>>(App.FilePath("config.json"));
            object src, dst, song;
            if (cfg.TryGetValue("replace_target",out src) && !String.IsNullOrEmpty(Convert.ToString(src)))
                rules.Rows.Add(src,cfg.TryGetValue("replace_text",out dst) ? dst : "");
            if (cfg.TryGetValue("song_db",out song) && !String.IsNullOrEmpty(Convert.ToString(song))) {
                var candidate = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(Convert.ToString(song))),"table");
                if (Directory.Exists(candidate)) folder.Text = candidate;
            }
        }
        if (rules.Rows.Count == 0) rules.Rows.Add();
    }

    internal static Dictionary<string,List<string>> IndexTables(string dir) {
        var index = new Dictionary<string,List<string>>(StringComparer.OrdinalIgnoreCase);
        var files = Directory.GetFiles(dir).Where(p => new[]{".bmt",".json"}.Contains(
            Path.GetExtension(p),StringComparer.OrdinalIgnoreCase)).OrderBy(p => p,StringComparer.Ordinal);
        foreach (string file in files) {
            try {
                using (Stream raw = File.OpenRead(file))
                using (Stream data = file.EndsWith(".bmt",StringComparison.OrdinalIgnoreCase)
                                    ? (Stream)new GZipStream(raw,CompressionMode.Decompress) : raw)
                using (var reader = new StreamReader(data)) {
                    var table = App.Json.Deserialize<CachedTable>(reader.ReadToEnd());
                    if (table == null || table.folder == null) continue;
                    foreach (var f in table.folder) {
                        if (String.IsNullOrWhiteSpace(f.name) || f.songs == null) continue;
                        string label = f.name.Replace('\r',' ').Replace('\n',' ').Trim();
                        foreach (var chart in f.songs) {
                            foreach (var hash in new[]{chart.sha256,chart.md5}) {
                                if (hash == null || !Regex.IsMatch(hash,@"\A(?:[0-9a-fA-F]{32}|[0-9a-fA-F]{64})\z")) continue;
                                string key = hash.ToLowerInvariant();
                                List<string> list;
                                if (!index.TryGetValue(key,out list)) index[key] = list = new List<string>();
                                if (!list.Contains(label)) list.Add(label);
                            }
                        }
                    }
                }
            } catch(Exception ex) { throw new InvalidDataException(Path.GetFileName(file)+" の読み込みに失敗しました。 "+ex.Message,ex); }
        }
        if (index.Count == 0) throw new InvalidDataException("譜面ハッシュを含む beatoraja 難易度表がありません。table フォルダーを選んでください。");
        return index;
    }

    void Save() {
        try {
            rules.EndEdit();
            var entries = new List<Rule>();
            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (DataGridViewRow row in rules.Rows) {
                string ch = Convert.ToString(row.Cells[0].Value), dst = Convert.ToString(row.Cells[1].Value);
                if (ch == "") continue;
                if (!((ch.Length == 1 && !Char.IsSurrogate(ch[0])) || (ch.Length == 2 && Char.IsSurrogatePair(ch,0))))
                    throw new ArgumentException((row.Index+1)+" 行目の対象文字を1文字にしてください。");
                if ((ch+dst).IndexOfAny(new[]{'\0','\r','\n'}) >= 0) throw new ArgumentException("改行は入力できません。");
                if (!seen.Add(ch)) throw new ArgumentException("対象文字が重複しています: "+ch);
                entries.Add(new Rule{target=ch,replacement=dst});
            }
            var options = new OutputSettings {rules=entries, difficulty=difficulty.Checked, folder=folder.Text.Trim()};
            options.labels = options.difficulty ? IndexTables(options.folder) : new Dictionary<string,List<string>>();
            App.Store(path, options);
            DialogResult = DialogResult.OK; Close();
        } catch(Exception ex) { MessageBox.Show(this,ex.Message,"出力設定"); }
    }
}
