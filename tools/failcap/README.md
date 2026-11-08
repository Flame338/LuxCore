# failcap

Captures failing Claude Code tool calls to a JSONL file. It is the first stage of a failure memory: capture now, search it with LuxCore later, and turn the reproducible cases into evals.

It is a hook script, not part of the LuxCore library. Standard library only, Python 3.9+.

## What it does

`.claude/settings.json` registers `failcap.py` on the `PostToolUseFailure` event. Every failed tool call appends one redacted line to `.claude/failcap/failures.jsonl` (git-ignored, file mode 0600). The hook never blocks a tool call and always exits 0.

Bash commands that exit non-zero count as failures: they arrive as `PostToolUseFailure` with `Exit code N` at the top of the error. Interrupts (you pressing Esc) are skipped because they say nothing about the tool or the approach.

## Record format (v1)

```json
{"v":1,"ts":"2026-11-08T08:22:16Z","event":"PostToolUseFailure","session_id":"…","tool_use_id":"…",
 "tool":"Bash","cwd":"/home/user/LuxCore","input":{"command":"make nope"},
 "error":"Exit code 2\nmake: *** No rule to make target nope.  Stop.","sig":"d85c97e59886e70c","exit_code":2}
```

- `input` is a trimmed copy of the tool input. Long strings are clipped (command 1000 chars, other fields 300).
- `error` keeps the head and the tail of long output, up to 4000 characters, because the useful line is usually last.
- `sig` is a 16-character hash of the error with paths, numbers and hex normalised. The same failure on another machine or on another day gets the same `sig`, which makes grouping and de-duplication a one-liner.
- `exit_code` is present only when known.

Useful one-liners:

```sh
jq -r '.sig' .claude/failcap/failures.jsonl | sort | uniq -c | sort -rn | head   # most repeated failures
jq -c 'select(.tool=="Bash") | {ts, cmd: .input.command, exit_code}' .claude/failcap/failures.jsonl
```

## Redaction

Best effort, not a guarantee. It removes private-key blocks, `Authorization` headers, bearer tokens, common token shapes (GitHub, `sk-…`, AWS, Slack, Google, JWT), `scheme://user:password@host` credentials, `name=value` pairs where the name contains `password`, `secret`, `token`, `api_key` and similar, and the literal values of secret-looking environment variables. It can miss secrets that have no recognisable shape, and it sometimes over-redacts. Treat the log as sensitive and keep it out of git.

## Configuration

| Variable | Meaning |
| --- | --- |
| `FAILCAP_LOG` | Path of the JSONL file. Default `$CLAUDE_PROJECT_DIR/.claude/failcap/failures.jsonl`. |
| `FAILCAP_MAX_BYTES` | Rotate to `<log>.1` once the file reaches this size. Default 20 MB. One generation is kept. |

To log failures from every project into one file, move the hook entry to `~/.claude/settings.json`, give it an absolute path to `failcap.py`, and set `FAILCAP_LOG`.

## Tests

```sh
python3 -m unittest discover -s tools/failcap -v
```

Not part of `make test` yet.

## Known limits

- The `PostToolUseFailure` payload fields (`error`, `is_interrupt`, `tool_use_id`) were checked against a live session on one Claude Code version. The script reads them defensively, but a future version could rename them. If records stop appearing or arrive empty, log a raw payload and compare.
- Only failures are captured, not what fixed them. Linking a failure to its eventual fix is the next stage.
- Cloud sessions are ephemeral. A log written in a container is lost with it, so use this on your own machine, or copy the file out before the session ends.
