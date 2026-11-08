"""Tests for failcap. Run: python3 -m unittest discover -s tools/failcap -v"""

import io
import json
import os
import stat
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import failcap  # noqa: E402


def failure(**over):
    payload = {
        "hook_event_name": "PostToolUseFailure",
        "session_id": "s1",
        "tool_use_id": "toolu_1",
        "tool_name": "Bash",
        "tool_input": {"command": "make test"},
        "error": "Exit code 2\nmake: *** [test] Error 1",
        "cwd": "/work/proj",
    }
    payload.update(over)
    return payload


class RedactTests(unittest.TestCase):
    def test_known_secret_shapes(self):
        cases = {
            "ghp_" + "a" * 36: "ghp_",
            "github_pat_" + "A1_" * 10: "github_pat_",
            "sk-ant-api03-" + "x" * 30: "sk-ant",
            "AKIA" + "ABCDEFGHIJKLMNOP": "AKIA",
            "xoxb-1234567890-abcdefghij": "xoxb",
            "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.abcdefghijk": "eyJ",
        }
        for secret, marker in cases.items():
            out = failcap.redact(f"failed with {secret} here")
            self.assertNotIn(secret, out, marker)
            self.assertIn("REDACTED", out, marker)

    def test_headers_urls_and_assignments(self):
        self.assertNotIn("hunter2hunter2", failcap.redact("Authorization: Bearer hunter2hunter2"))
        self.assertNotIn("p4ssw0rd", failcap.redact("git clone https://bob:p4ssw0rd@host/x.git"))
        self.assertIn("host/x.git", failcap.redact("git clone https://bob:p4ssw0rd@host/x.git"))
        self.assertNotIn("s3cr3tvalue", failcap.redact("export API_TOKEN=s3cr3tvalue"))
        self.assertNotIn("abcdef123456", failcap.redact('{"password": "abcdef123456"}'))

    def test_private_key_block_including_truncated(self):
        block = "-----BEGIN RSA PRIVATE KEY-----\nMIIabc\ndef\n-----END RSA PRIVATE KEY-----"
        self.assertEqual(failcap.redact(f"x {block} y"), "x [REDACTED:private-key] y")
        self.assertNotIn("MIIabc", failcap.redact("-----BEGIN PRIVATE KEY-----\nMIIabc"))

    def test_environment_secret_values(self):
        env = {"MY_SERVICE_TOKEN": "zzz-not-a-known-shape-123", "HOME": "/root", "SHORT_SECRET": "abc"}
        out = failcap.redact("curl failed: zzz-not-a-known-shape-123 /root abc", env)
        self.assertNotIn("zzz-not-a-known-shape-123", out)
        self.assertIn("/root", out)  # HOME is not secret-named
        self.assertIn("abc", out)  # too short to treat as a secret

    def test_ordinary_error_text_is_untouched(self):
        text = "error: expected ';' before '}' token\nfoo.cpp:12:5: note: here"
        self.assertEqual(failcap.redact(text), text)

    def test_no_pathological_slowdown_on_long_identifier_runs(self):
        blob = "A" * 50_000
        start = time.monotonic()
        failcap.redact(blob)
        self.assertLess(time.monotonic() - start, 2.0)


class ClipTests(unittest.TestCase):
    def test_keeps_head_and_tail(self):
        text = "HEAD" + "." * 10_000 + "TAIL"
        out = failcap.clip(text, 400)
        self.assertTrue(out.startswith("HEAD"))
        self.assertTrue(out.endswith("TAIL"))
        self.assertIn("chars omitted", out)
        self.assertLess(len(out), 500)

    def test_short_text_unchanged(self):
        self.assertEqual(failcap.clip("abc", 400), "abc")


class SignatureTests(unittest.TestCase):
    def test_same_failure_different_paths_numbers_hashes(self):
        a = failcap.signature("Bash", "Error: /home/a/proj/x.cpp:12: bad thing 0xdeadbeef at 1234")
        b = failcap.signature("Bash", "Error: /Users/b/other/y.cpp:99: bad thing 0xcafef00d at 5")
        self.assertEqual(a, b)

    def test_different_failures_differ(self):
        self.assertNotEqual(
            failcap.signature("Bash", "command not found: foo"),
            failcap.signature("Bash", "permission denied: foo"),
        )

    def test_tool_name_is_part_of_signature(self):
        self.assertNotEqual(failcap.signature("Bash", "x"), failcap.signature("Edit", "x"))


class BuildRecordTests(unittest.TestCase):
    def test_failure_event_is_recorded(self):
        rec = failcap.build_record(failure())
        self.assertEqual(rec["v"], 1)
        self.assertEqual(rec["tool"], "Bash")
        self.assertEqual(rec["exit_code"], 2)
        self.assertEqual(rec["input"], {"command": "make test"})
        self.assertIn("make: ***", rec["error"])
        self.assertEqual(len(rec["sig"]), 16)
        self.assertTrue(rec["ts"].endswith("Z"))

    def test_non_bash_failure_has_no_exit_code(self):
        rec = failcap.build_record(
            failure(tool_name="Edit", tool_input={"file_path": "a.cpp", "old_string": "x"}, error="String not found")
        )
        self.assertNotIn("exit_code", rec)
        self.assertEqual(rec["input"]["file_path"], "a.cpp")

    def test_interrupts_are_not_failures(self):
        self.assertIsNone(failcap.build_record(failure(is_interrupt=True)))

    def test_post_tool_use_bash_nonzero_is_recorded(self):
        payload = {
            "hook_event_name": "PostToolUse",
            "tool_name": "Bash",
            "tool_input": {"command": "false"},
            "tool_response": {"stdout": "", "stderr": "boom", "exit_code": 1},
        }
        rec = failcap.build_record(payload)
        self.assertEqual(rec["exit_code"], 1)
        self.assertIn("boom", rec["error"])

    def test_post_tool_use_success_and_unknown_exit_are_ignored(self):
        ok = {"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_response": {"stdout": "hi", "exit_code": 0}}
        unknown = {"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_response": {"stdout": "hi"}}
        other_tool = {"hook_event_name": "PostToolUse", "tool_name": "Read", "tool_response": {"exit_code": 1}}
        for payload in (ok, unknown, other_tool):
            self.assertIsNone(failcap.build_record(payload))

    def test_unknown_event_and_missing_tool_are_ignored(self):
        self.assertIsNone(failcap.build_record(failure(hook_event_name="Stop")))
        self.assertIsNone(failcap.build_record(failure(tool_name=None)))

    def test_secrets_never_reach_the_record(self):
        secret = "ghp_" + "b" * 36
        rec = failcap.build_record(
            failure(
                tool_input={"command": f"git push https://x:{secret}@github.com/a/b"},
                error=f"Exit code 128\nremote: denied for {secret}",
            )
        )
        self.assertNotIn(secret, json.dumps(rec))

    def test_large_error_is_clipped_with_tail_kept(self):
        rec = failcap.build_record(failure(error="Exit code 1\n" + "noise\n" * 20_000 + "FINAL LINE"))
        self.assertLess(len(rec["error"]), failcap.MAX_TEXT + 100)
        self.assertTrue(rec["error"].endswith("FINAL LINE"))

    def test_list_shaped_error_is_flattened(self):
        rec = failcap.build_record(failure(error=[{"text": "first"}, {"text": "second"}]))
        self.assertIn("first", rec["error"])
        self.assertIn("second", rec["error"])


class MainTests(unittest.TestCase):
    def run_main(self, payload_bytes, tmp, extra_env=None):
        env = {"FAILCAP_LOG": os.path.join(tmp, "sub", "failures.jsonl")}
        env.update(extra_env or {})
        code = failcap.main(io.BytesIO(payload_bytes), env)
        return code, env["FAILCAP_LOG"]

    def read_log(self, path):
        with open(path, encoding="utf-8") as fh:
            return [json.loads(line) for line in fh]

    def test_writes_one_json_line_per_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, path = self.run_main(json.dumps(failure()).encode(), tmp)
            self.assertEqual(code, 0)
            code, _ = self.run_main(json.dumps(failure(tool_use_id="toolu_2")).encode(), tmp)
            rows = self.read_log(path)
            self.assertEqual([r["tool_use_id"] for r in rows], ["toolu_1", "toolu_2"])

    def test_log_is_private(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, path = self.run_main(json.dumps(failure()).encode(), tmp)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)

    def test_same_tool_use_id_is_logged_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.run_main(json.dumps(failure()).encode(), tmp)
            _, path = self.run_main(json.dumps(failure()).encode(), tmp)
            self.assertEqual(len(self.read_log(path)), 1)

    def test_nothing_written_for_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            payload = {"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_response": {"exit_code": 0}}
            _, path = self.run_main(json.dumps(payload).encode(), tmp)
            self.assertFalse(os.path.exists(path))

    def test_garbage_input_never_raises_and_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            for blob in (b"", b"not json", b"[1,2,3]", b"null", b"\xff\xfe\x00", b'{"tool_name": 5}'):
                code, path = self.run_main(blob, tmp)
                self.assertEqual(code, 0, blob)
                self.assertFalse(os.path.exists(path), blob)

    def test_unwritable_log_still_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            blocker = os.path.join(tmp, "file")
            open(blocker, "w").close()
            env = {"FAILCAP_LOG": os.path.join(blocker, "nested", "failures.jsonl")}
            self.assertEqual(failcap.main(io.BytesIO(json.dumps(failure()).encode()), env), 0)

    def test_rotates_when_over_size_limit(self):
        with tempfile.TemporaryDirectory() as tmp:
            _, path = self.run_main(json.dumps(failure()).encode(), tmp, {"FAILCAP_MAX_BYTES": "1"})
            self.run_main(json.dumps(failure(tool_use_id="toolu_2")).encode(), tmp, {"FAILCAP_MAX_BYTES": "1"})
            self.assertTrue(os.path.exists(path + ".1"))
            self.assertEqual([r["tool_use_id"] for r in self.read_log(path)], ["toolu_2"])

    def test_default_path_uses_project_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            env = {"CLAUDE_PROJECT_DIR": tmp}
            failcap.main(io.BytesIO(json.dumps(failure()).encode()), env)
            self.assertTrue(os.path.exists(os.path.join(tmp, ".claude", "failcap", "failures.jsonl")))


if __name__ == "__main__":
    unittest.main()
