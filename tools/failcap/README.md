# failcap

Captures failing Claude Code tool calls to a local JSONL file, from every project, so you can search them later, spot repeat failures, and turn the reproducible ones into evals.

One file, Python standard library only, no install step beyond running it once. Works on Linux, macOS and Windows.

## Install

```sh
git clone <this repo> failcap
python failcap/failcap.py install
```

That registers a `PostToolUseFailure` hook in `~/.claude/settings.json` (or `$CLAUDE_CONFIG_DIR/settings.json`). Open `/hooks` in Claude Code or start a new session to load it. Check it with:

```sh
python failcap/failcap.py status
```

`install` merges into your existing settings: it keeps every other setting and hook, replaces an older failcap entry if there is one, writes atomically, and keeps the first backup of the original as `settings.json.failcap.bak`. It refuses to touch a file that is not valid JSON. `--dry-run` shows the result without writing, `--settings PATH` targets another file, and `--python PATH` chooses the interpreter the hook uses.

The hook is registered in exec form (`command` plus `args`, no shell), so the same entry works under bash and PowerShell with no quoting. It stores the absolute path of this checkout and of the Python that ran `install`. If you move the folder or change Python, run `install` again.

`python failcap/failcap.py uninstall` removes the hook and leaves the log.

## What gets captured

Every failed tool call, from any tool: a Bash command that exits non-zero, a Read of a missing file, an Edit whose text was not found. User interrupts (Esc) are skipped, because they say nothing about the tool or the approach.

The log is `~/.claude/failcap/failures.jsonl`, one JSON object per line, file mode 0600, rotated to `failures.jsonl.1` at 20 MB. Records from all projects go to the same file and carry a `project` name.

```json
{"v":1,"ts":"2026-11-08T08:44:50Z","event":"PostToolUseFailure","project":"myrepo",
 "session_id":"…","tool_use_id":"…","tool":"Bash","cwd":"/home/me/myrepo",
 "input":{"command":"make nope"},
 "error":"Exit code 2\nmake: *** No rule to make target nope.  Stop.",
 "sig":"d85c97e59886e70c","exit_code":2}
```

- `project` is the name of the nearest directory containing `.git`, or the directory name if there is none.
- `input` is a trimmed copy of the tool input. Long strings are clipped (a command to 1000 characters, other fields to 300).
- `error` keeps the head and the tail of long output, up to 4000 characters, because the useful line is usually last.
- `sig` is a 16-character hash of the error with paths, numbers and hex normalised. The same failure on another machine or another day gets the same `sig`.
- `exit_code` is present only when known.

```sh
jq -r '.sig' ~/.claude/failcap/failures.jsonl | sort | uniq -c | sort -rn | head            # most repeated failures
jq -c 'select(.project=="myrepo" and .tool=="Bash") | {ts, cmd: .input.command, exit_code}' ~/.claude/failcap/failures.jsonl
```

## Keeping a project out of the log

The central log mixes every project. To keep some out:

- Create an empty file named `.failcap-ignore` in a project, or in any parent folder. Everything below it is skipped, so one marker in `~/clients/` covers every repo in there.
- Or set `FAILCAP_DISABLE=1`, for example in a project's `.claude/settings.local.json` under `"env"`.

## Redaction

Best effort, not a guarantee. It removes private-key blocks, `Authorization` headers, bearer tokens, common token shapes (GitHub, `sk-…`, AWS, Slack, Google, JWT), `scheme://user:password@host` credentials, `name=value` and `"name": "value"` pairs where the name contains `password`, `secret`, `token`, `api_key` and similar, and the literal values of secret-looking environment variables. It can miss secrets that have no recognisable shape, and it sometimes over-redacts. Treat the log as sensitive, and keep it out of git and out of anything you share.

## Configuration

| Variable | Meaning |
| --- | --- |
| `FAILCAP_LOG` | Path of the JSONL file. Default `~/.claude/failcap/failures.jsonl`. |
| `FAILCAP_MAX_BYTES` | Rotate to `<log>.1` once the file reaches this size. Default 20 MB. One generation is kept. |
| `FAILCAP_DISABLE` | `1`, `true` or `yes` turns capture off for that environment. |
| `CLAUDE_CONFIG_DIR` | Moves the default log and the default settings file with Claude Code's own config directory. |

## Cloud sessions

A hook in your home directory does not reach Claude Code cloud sessions, because the container has its own filesystem. To capture there, copy `failcap.py` into the repository and register it in the repo's `.claude/settings.json`:

```json
{"hooks": {"PostToolUseFailure": [{"hooks": [
  {"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR:-.}/path/to/failcap.py\"", "timeout": 10}
]}]}}
```

The log is lost when the container is reclaimed, so copy it out before the session ends if you want to keep it.

## Behaviour you can rely on

- It never blocks a tool call. It always exits 0, reads at most 8 MB of input, bounds its regex cost, and reports its own errors to stderr only.
- A tool call that fires more than one event is logged once, matched on `tool_use_id`.
- It does nothing for successful calls.

## Tests

```sh
python -m unittest discover -v
```

CI (`.github/workflows/test.yml`) runs them on Linux, Windows and macOS with Python 3.9, 3.12 and 3.13. Locally the suite has been run on Python 3.9, 3.11, 3.12 and 3.13 on Linux.

## Known limits

- The `PostToolUseFailure` fields (`error`, `is_interrupt`, `tool_use_id`) were checked against a live session on one Claude Code version. The script reads them defensively, but a future version could rename them. If records stop appearing or arrive empty, log a raw payload and compare.
- The exec-form hook (`command` plus `args`) needs a Claude Code version that supports it. Run `/hooks` after installing to confirm it loaded.
- Only failures are captured, not what fixed them. Linking a failure to its eventual fix is a separate stage.
- The Windows path has been reasoned about and covered by CI configuration, but not run on a Windows machine by the author yet.
