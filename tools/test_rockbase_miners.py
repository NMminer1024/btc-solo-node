#!/usr/bin/env python3
"""Regression tests for the Rockbase miner cache and history scripts."""

import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get(
    "ROCKBASE_MINERS_SOURCE_DIR",
    ROOT / "buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay",
))
CACHE_SCRIPT = SOURCE / "usr/bin/rockbase_miners_cache.sh"
HISTORY_SCRIPT = SOURCE / "usr/bin/rockbase_miners_historyd.sh"
DECAY_AWK = SOURCE / "usr/share/rockbase/hashrate-decay.awk"
WINDOWS = (60, 300, 900, 3600, 21600, 86400, 604800)
KEYS = (
    "hashrate1m", "hashrate5m", "hashrate15m", "hashrate1hr",
    "hashrate6hr", "hashrate1d", "hashrate7d",
)


def ghs(value):
    scales = {"K": 1e-6, "M": 1e-3, "G": 1, "T": 1e3, "P": 1e6, "E": 1e9}
    if value[-1:] in scales:
        return float(value[:-1]) * scales[value[-1]]
    return float(value)


def reference_decay(value, age, window):
    """Independent reference using ckpool's real exp-based recurrence."""
    steps = int(max(age, 0) / 1.875)
    factor = 1 / (2 - math.exp(-1.875 / window))
    for _ in range(steps):
        value *= factor
    remainder = max(age, 0) - steps * 1.875
    value /= 2 - math.exp(-remainder / window)
    return 0 if value < 1e-9 else value


class MinerCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.bin = self.base / "bin"
        self.bin.mkdir()
        for name, contents in {
            "date": '#!/bin/sh\nif [ "$1" = "+%s" ]; then echo "$TEST_NOW"; '
                    'else exec /bin/date "$@"; fi\n',
            "pidof": '#!/bin/sh\n[ "$TEST_POOL_RUNNING" = 1 ]\n',
            "logger": '#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_LOG"\n',
        }.items():
            path = self.bin / name
            path.write_text(contents)
            path.chmod(0o755)
        self.now = 1791532000
        self.cache_dir = self.base / "cache"
        self.cache_dir.mkdir()
        self.pool = self.base / "pool.status"
        self.users = self.base / "users"
        self.users.mkdir()
        self.user = self.users / "testWallet"
        self.history = self.base / "history"
        self.history.mkdir()
        self.env = {
            **os.environ,
            "PATH": f"{self.bin}:{os.environ['PATH']}",
            "TEST_NOW": str(self.now),
            "TEST_POOL_RUNNING": "1",
            "TEST_LOG": str(self.base / "errors.log"),
            "POOL_STATUS": str(self.pool),
            "USERS_DIR": str(self.users),
            "CKPOOL_USERS_DIR": str(self.users),
            "CACHE_DIR": str(self.cache_dir),
            "MINERS_CACHE": str(self.cache_dir / "miners.json"),
            "MINERS_HISTORY_DIR": str(self.history),
            "MINERS_HISTORY_CACHE_DIR": str(self.cache_dir),
            "HASHRATE_DECAY_AWK": str(DECAY_AWK),
        }
        self.fixture()

    def fixture(self, age=0, share_age=None):
        timestamp = self.now - age
        share = self.now - (age if share_age is None else share_age)
        self.pool.write_text(
            json.dumps({"lastupdate": timestamp, "Users": 1, "Workers": 1}) + "\n"
            + json.dumps(dict.fromkeys(KEYS, "10T")) + "\n"
            + json.dumps({"accepted": 12345, "bestshare": 97000557}) + "\n"
        )
        worker = {
            "workername": "testWallet.worker1", "lastshare": share,
            **dict.fromkeys(KEYS, "10T"), "shares": 12345,
            "bestshare": 97000557, "bestever": 97000557,
        }
        miner = {
            **dict.fromkeys(KEYS, "10T"), "lastshare": share, "workers": 1,
            "shares": 12345, "bestshare": 97000557, "bestever": 97000557,
            "worker": [worker],
        }
        self.user.write_text(json.dumps(miner, indent=4) + "\n")
        os.utime(self.user, (timestamp, timestamp))

    def run_script(self, script, check=True):
        return subprocess.run(
            ["sh", str(script), "once"], env=self.env,
            capture_output=True, text=True, check=check,
        )

    def cache(self):
        self.run_script(CACHE_SCRIPT)
        return json.loads((self.cache_dir / "miners.json").read_text())

    def run_history(self):
        self.run_script(HISTORY_SCRIPT)
        return json.loads((self.cache_dir / "miners-history-node.json").read_text())

    def advance(self, seconds):
        self.now += seconds
        self.env["TEST_NOW"] = str(self.now)

    def test_live_values_and_history_unchanged(self):
        data = self.cache()
        self.assertFalse(data["pool"]["estimated"])
        self.assertTrue(data["pool"]["running"])
        self.assertEqual(data["pool"]["hashrate5m"], "10T")
        self.assertTrue(data["miners"][0]["online"])
        history = self.run_history()
        self.assertEqual(history["points"][-1]["ts"], self.now)
        self.assertFalse(history["points"][-1]["estimated"])
        self.assertEqual(history["points"][-1]["h5m"], "10T")

    def test_stopped_pool_decays_every_window_and_keeps_scores(self):
        self.fixture(age=19500)
        self.env["TEST_POOL_RUNNING"] = "0"
        data = self.cache()
        self.assertTrue(data["pool"]["estimated"])
        self.assertEqual(data["pool"]["workers"], 0)
        self.assertEqual(data["pool"]["users"], 0)
        for key, window in zip(KEYS, WINDOWS):
            actual = ghs(data["pool"][key])
            expected = reference_decay(10000, 19500, window)
            self.assertAlmostEqual(actual, expected, delta=max(1e-9, expected * 6e-6))
        self.assertEqual(data["pool"]["hashrate5m"], "0")
        self.assertGreater(ghs(data["pool"]["hashrate6hr"]), 0)
        self.assertLess(ghs(data["pool"]["hashrate6hr"]), ghs(data["pool"]["hashrate1d"]))
        self.assertEqual(data["pool"]["accepted"], 12345)
        self.assertEqual(data["pool"]["bestshare"], 97000557)
        miner = data["miners"][0]
        self.assertFalse(miner["online"])
        self.assertEqual(miner["workers"], 0)
        self.assertEqual(miner["hashrate1d"], data["pool"]["hashrate1d"])
        self.assertEqual(miner["workers_detail"][0]["hashrate1d"], miner["hashrate1d"])

    def test_stopped_recent_pool_is_offline_but_retains_window_memory(self):
        self.fixture(age=120)
        self.env["TEST_POOL_RUNNING"] = "0"
        data = self.cache()
        self.assertFalse(data["pool"]["stale"])
        self.assertTrue(data["pool"]["estimated"])
        self.assertFalse(data["miners"][0]["online"])
        self.assertFalse(data["miners"][0]["workers_detail"][0]["online"])
        self.assertGreater(ghs(data["pool"]["hashrate5m"]), 0)
        self.assertLess(ghs(data["pool"]["hashrate5m"]), 10000)

    def test_stalled_live_process_is_not_treated_as_fresh(self):
        self.fixture(age=301)
        data = self.cache()
        self.assertTrue(data["pool"]["running"])
        self.assertTrue(data["pool"]["stale"])
        self.assertTrue(data["pool"]["estimated"])
        self.assertFalse(data["miners"][0]["online"])

    def test_history_advances_deduplicates_and_preserves_old_points(self):
        self.fixture(age=19500)
        self.env["TEST_POOL_RUNNING"] = "0"
        cache = self.cache()
        history = self.run_history()
        first = history["points"][-1]
        self.assertEqual(first["ts"], self.now)
        self.assertTrue(first["estimated"])
        self.assertEqual(first["source_lastupdate"], self.now - 19500)
        self.assertEqual(first["h6h"], cache["pool"]["hashrate6hr"])
        self.assertEqual(self.run_history()["raw_points"], 1)
        user_history = next(self.history.glob("user-testWallet-*.jsonl"))
        row = json.loads(user_history.read_text().splitlines()[-1])
        self.assertEqual(row["h1d"], cache["miners"][0]["hashrate1d"])
        self.assertTrue(row["estimated"])
        self.assertEqual(row["workers"], 0)
        self.advance(130)
        next_cache = self.cache()
        next_history = self.run_history()
        self.assertEqual(next_history["raw_points"], 2)
        self.assertEqual(next_history["points"][0], first)
        self.assertGreater(next_history["points"][-1]["ts"], first["ts"])
        self.assertLess(ghs(next_cache["pool"]["hashrate6hr"]), ghs(cache["pool"]["hashrate6hr"]))
        self.assertEqual(next_history["points"][-1]["h6h"], next_cache["pool"]["hashrate6hr"])

    def test_restart_is_idempotent_and_recovery_uses_new_raw_values(self):
        self.fixture(age=600)
        self.env["TEST_POOL_RUNNING"] = "0"
        data = self.cache()
        self.assertEqual(self.cache()["pool"], data["pool"])
        self.advance(120)
        self.fixture()
        self.env["TEST_POOL_RUNNING"] = "1"
        recovered = self.cache()
        self.assertFalse(recovered["pool"]["estimated"])
        self.assertEqual(recovered["pool"]["hashrate5m"], "10T")
        self.assertTrue(recovered["miners"][0]["online"])

    def test_future_timestamp_does_not_amplify_hashrate(self):
        self.fixture(age=-100)
        self.env["TEST_POOL_RUNNING"] = "0"
        data = self.cache()
        self.assertEqual(data["pool"]["age_seconds"], 0)
        self.assertEqual(ghs(data["pool"]["hashrate5m"]), 10000)

    def test_user_mtime_prevents_double_decay_from_old_lastshare(self):
        self.fixture(age=0, share_age=3600)
        data = self.cache()
        self.assertFalse(data["miners"][0]["online"])
        self.assertFalse(data["miners"][0]["estimated"])
        self.assertEqual(data["miners"][0]["hashrate1d"], "10T")

    def test_missing_pool_is_unavailable_and_does_not_fabricate_history(self):
        self.pool.unlink()
        self.env["TEST_POOL_RUNNING"] = "0"
        data = self.cache()
        self.assertFalse(data["ok"])
        self.assertEqual(data["pool"]["hashrate5m"], "0")
        self.assertEqual(self.run_history()["raw_points"], 0)

    def test_decay_failure_preserves_published_cache_and_reports_error(self):
        previous = self.cache()
        self.fixture(age=600)
        self.env["HASHRATE_DECAY_AWK"] = str(self.base / "missing.awk")
        result = self.run_script(CACHE_SCRIPT, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot decay pool", result.stderr)
        self.assertEqual(json.loads((self.cache_dir / "miners.json").read_text()), previous)
        self.assertIn("Cannot decay pool", (self.base / "errors.log").read_text())

    def test_invalid_history_cache_timestamp_is_reported(self):
        self.cache()
        (self.cache_dir / "miners.json").write_text('{"ok":true,"generated_at":0}\n')
        result = self.run_script(HISTORY_SCRIPT, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Invalid miner cache timestamp", result.stderr)

    def test_decay_precision_units_partial_steps_and_zero_threshold(self):
        for age in (0, 1, 300, 1800, 21600, 604800):
            with self.subTest(age=age):
                values = ("10000000M", "10T", "10000G", "0.01P", "10T", "10T", "10T")
                result = subprocess.run(
                    ["awk", "-v", f"age={age}", "-f", str(DECAY_AWK)],
                    input="\n".join(values) + "\n", text=True, capture_output=True, check=True,
                )
                actual = result.stdout.splitlines()
                self.assertEqual(len(actual), 7)
                for value, window in zip(actual, WINDOWS):
                    expected = reference_decay(10000, age, window)
                    self.assertAlmostEqual(ghs(value), expected, delta=max(1e-9, expected * 6e-6))


if __name__ == "__main__":
    unittest.main()
