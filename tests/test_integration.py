"""Portable core integration tests (no OBS required)."""
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
import unittest
from datetime import datetime, timezone

PROJECT = Path(__file__).resolve().parent.parent
BIN = PROJECT / "build" / "timestamp_test"

class TestTimestamps(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)
        shutil.copy2(BIN, self.path / "timestamp_test")
        (self.path / "sessions").mkdir()
        self.song = self.path / "songdata.db"
        self.score = self.path / "score.db"
        self.log = self.path / "scoredatalog.db"
        with sqlite3.connect(self.song) as conn:
            conn.executescript("CREATE TABLE song(sha256 TEXT, title TEXT, subtitle TEXT, artist TEXT, length INTEGER, adddate INTEGER);")
            conn.execute("INSERT INTO song VALUES (?,?,?,?,?,?)", ("a"*64, "テスト曲", "[HYPER]", "作曲者", 60000, 10))
        with sqlite3.connect(self.score) as conn:
            conn.execute("CREATE TABLE score(sha256 TEXT, date INTEGER)")
        with sqlite3.connect(self.log) as conn:
            conn.execute("CREATE TABLE scoredatalog(sha256 TEXT, date INTEGER)")
        self.base = int(datetime(2026, 11, 16, 12, 0, tzinfo=timezone.utc).timestamp())
        with sqlite3.connect(self.log) as conn:
            conn.executemany("INSERT INTO scoredatalog VALUES (?,?)", [("a"*64, self.base+100), ("a"*64, self.base+200)])
        with sqlite3.connect(self.score) as conn:
            conn.execute("INSERT INTO score VALUES (?,?)", ("a"*64,self.base+200))
        self.write_config()
    def write_config(self, **updates):
        d={"song_db": str(self.song), "score_db": str(self.score), "scoredatalog_db": str(self.log),
           "obs": {"host":"localhost", "port": 4455, "password":""},
           "estimate_song_start": True, "offset_seconds": 0, "include_artist": False,
           "auto_generate": True}
        d.update(updates)
        (self.path / "config.json").write_text(json.dumps(d, ensure_ascii=False), encoding="utf8")
    def run_bin(self, *args, rc=0):
        p=subprocess.run([str(self.path / "timestamp_test"), *args], cwd=self.path, capture_output=True, text=True)
        self.assertEqual(p.returncode, rc, (p.stdout, p.stderr))
        return p
    def test_multiple_plays_and_regeneration(self):
        self.run_bin("start", "--at", "2026-11-16T12:00:00+00:00")
        self.assertTrue((self.path / "active_session.json").exists())
        self.run_bin("stop", "--at", "2026-11-16T12:05:00+00:00")
        self.assertFalse((self.path / "active_session.json").exists())
        outputs=list((self.path / "timestamps").glob("*_timestamp.txt"))
        self.assertEqual(len(outputs), 1)
        self.assertFalse(list((self.path / "sessions").glob("*_timestamp*.txt")))
        self.assertEqual(len(list((self.path / "sessions").glob("*_report.txt"))), 1)
        text=outputs[0].read_text("utf-8-sig")
        self.assertEqual(text.splitlines(), ["0:00 配信開始", "0:40 テスト曲 [HYPER]", "2:20 テスト曲 [HYPER]", "5:00 配信終了"])
        self.run_bin("generate", "--offset", "30", "--basis", "result")
        self.assertEqual(outputs[0].read_text("utf-8-sig").splitlines()[1], "1:10 テスト曲 [HYPER]")
    def test_replacement_and_regeneration(self):
        with sqlite3.connect(self.song) as conn:
            conn.execute("UPDATE song SET title = ?, artist = ?", ("♪テスト♪", "作曲者♪"))
        self.write_config(replace_target="♪", replace_text='♪"曲" ', include_artist=True)
        self.run_bin("start", "--at", "2026-11-16T12:00:00Z")
        self.run_bin("stop", "--at", "2026-11-16T12:05:00Z")
        output = next((self.path / "timestamps").glob("*_timestamp.txt"))
        self.assertEqual(output.read_text("utf-8-sig").splitlines()[1],
                         '0:40 ♪"曲" テスト♪"曲"  [HYPER] / 作曲者♪"曲" ')
        self.write_config(replace_target="♪", replace_text="")
        self.run_bin("generate")
        self.assertEqual(output.read_text("utf-8-sig").splitlines()[1], "0:40 テスト [HYPER]")
        self.write_config(replace_target=":", replace_text="→")
        self.run_bin("generate")
        self.assertTrue(output.read_text("utf-8-sig").startswith("0:00 配信開始\n0:40 "))
        self.write_config(replace_target="ab", replace_text="x")
        p = self.run_bin("generate", rc=1)
        self.assertIn("対象文字は1文字", p.stdout)

    def test_autogenerate_disabled(self):
        self.write_config(auto_generate=False)
        self.run_bin("start", "--at", "2026-11-16T12:00:00Z")
        self.run_bin("stop", "--at", "2026-11-16T12:05:00Z")
        self.assertFalse(list((self.path / "timestamps").glob("*_timestamp.txt")))
        self.run_bin("generate")
        self.assertTrue(list((self.path / "timestamps").glob("*_timestamp.txt")))

    def test_manual_bad_time_and_double_start(self):
        self.run_bin("start", "--at", "2026-11-16T12:00:00Z")
        self.run_bin("start", "--at", "2026-11-16T12:00:01Z")
        self.assertEqual(len(list((self.path / "sessions").glob("*.json"))),1)
        self.run_bin("stop", "--at", "2026-11-16T11:59:59Z",rc=1)
        self.assertTrue((self.path/"active_session.json").exists())
        self.run_bin("stop", "--at", "2026-11-16T12:05:00Z")

    def test_offset_mmss(self):
        self.run_bin("start", "--at", "2026-11-16T12:00:00Z")
        self.run_bin("stop", "--at", "2026-11-16T12:05:00Z")
        self.run_bin("generate", "--offset", "2:40")
        text=list((self.path / "timestamps").glob("*_timestamp.txt"))[0].read_text("utf-8-sig")
        self.assertIn("0:00 テスト曲",text)
        self.assertIn("[HYPER]",text)

    def test_score_fallback(self):
        self.log.unlink()
        self.run_bin("start", "--at", "2026-11-16T12:00:00Z")
        self.run_bin("stop", "--at", "2026-11-16T12:05:00Z")
        outputs=list((self.path / "timestamps").glob("*_timestamp.txt"))
        self.assertEqual(len(outputs[0].read_text("utf-8-sig").splitlines()),3)
        report=list((self.path / "sessions").glob("*_report.txt"))[0].read_text("utf-8-sig")
        self.assertIn("score.db",report)
        self.assertIn("同じ譜面の再プレー",report)
    def test_reject_missing_db(self):
        self.song.unlink()
        p=self.run_bin("start",rc=1)
        self.assertIn("song_db",p.stdout)
    def test_local_time_offset(self):
        self.run_bin("start","--at","2026-11-16T21:00:00+09:00")
        self.run_bin("stop","--at","2026-11-16T21:05:00+09:00")
        outputs=list((self.path / "timestamps").glob("*_timestamp.txt"))
        self.assertIn("0:40 テスト曲",outputs[0].read_text("utf-8-sig"))
    def test_missing_title_and_negative_clamp(self):
        with sqlite3.connect(self.log) as conn:
            conn.execute("INSERT INTO scoredatalog VALUES (?,?)",("b"*64,self.base+20))
        self.run_bin("start","--at","2026-11-16T12:00:00Z")
        self.run_bin("stop","--at","2026-11-16T12:05:00Z")
        text=list((self.path/"timestamps").glob("*_timestamp.txt"))[0].read_text("utf-8-sig")
        self.assertIn("[曲名不明",text)
        self.assertIn("0:40 テスト曲",text)
    def test_python_v1_session_reuse(self):
        manual=self.path/"sessions"/"legacy.json"
        manual.write_text(json.dumps({"start":"2026-11-16T21:00:00+09:00", "end":"2026-11-16T21:05:00+09:00",
                   "start_method":"obs_event","end_method":"obs_event"}),encoding="utf8")
        self.run_bin("generate","--session",str(manual))
        self.assertEqual((self.path/"timestamps"/"legacy_timestamp.txt").read_text("utf-8-sig").splitlines()[1],"0:40 テスト曲 [HYPER]")

if __name__ == "__main__": unittest.main()
