#!/usr/bin/env python3
"""failcap: record failing Claude Code tool calls to a JSONL log.

Two modes, chosen by how it is started:

  hook mode     no arguments. Claude Code runs it on PostToolUseFailure with the
                event payload (JSON) on stdin. It appends one redacted record to
                the log when the call failed. It is deliberately boring: standard
                library only, never blocks a tool call, never raises, always
                exits 0.

  command mode  failcap.py install | uninstall | status [options]
                installs or removes the hook in a Claude Code settings file, and
                reports whether capture is working.

Events handled
  PostToolUseFailure   any tool that failed. Bash non-zero exits arrive here
                       (verified in a live session), so this is the only event
                       `install` registers.
  PostToolUse          only Bash, and only when the response shows a non-zero
                       exit code. Not registered by default; kept as a fallback
                       in case a Claude Code version reports Bash failures
                       through PostToolUse instead.

Log location (first match wins)
  $FAILCAP_LOG
  $CLAUDE_CONFIG_DIR/failcap/failures.jsonl
  ~/.claude/failcap/failures.jsonl

One central log is the default so failures from every project end up in one
searchable place. Each record carries a `project` name.

Opting out
  FAILCAP_DISABLE=1       environment variable (for example in a project's
                          .claude/settings.local.json "env" block)
  .failcap-ignore         empty marker file in a project directory, or in any
                          parent directory, to skip everything below it

Record schema (v1), one JSON object per line:
  v, ts, event, project, session_id, tool_use_id, tool, cwd, input, error,
  exit_code (when known), sig
`sig` is a short hash of the error text with paths, numbers and hashes
normalised, so the same failure on different machines or runs groups together.

Redaction is best effort, not a guarantee. See redact().
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from datetime import datetime, timezone

SCHEMA_VERSION = 1
MAX_STDIN = 8 * 1024 * 1024  # never read more than this from the hook payload
MAX_TEXT = 4000  # characters kept per text field after clipping
PRE_CLIP = 50_000  # clip before running regexes so their cost stays bounded
DEFAULT_MAX_BYTES = 20 * 1024 * 1024
DEDUPE_TAIL_BYTES = 64 * 1024
IGNORE_MARKER = ".failcap-ignore"
HOOK_EVENT = "PostToolUseFailure"
HOOK_TIMEOUT_SECONDS = 10

# --------------------------------------------------------------------------
# Redaction
# --------------------------------------------------------------------------

# Bounded quantifiers on purpose: an unbounded prefix would make this regex
# quadratic on long runs of identifier characters (e.g. base64 blobs).
_SECRET_NAME = (
    r"[A-Za-z0-9_.-]{0,40}"
    r"(?:password|passwd|secret|token|api[_-]?key|access[_-]?key|"
    r"private[_-]?key|credential)"
    r"[A-Za-z0-9_.-]{0,40}"
)

_PATTERNS: list[tuple[re.Pattern[str], str]] = [
    (
        re.compile(
            r"-----BEGIN [A-Z ]*PRIVATE KEY-----.*?(?:-----END [A-Z ]*PRIVATE KEY-----|\Z)",
            re.S,
        ),
        "[REDACTED:private-key]",
    ),
    (
        re.compile(r"(?i)\b(authorization\s*[:=]\s*)(?:bearer\s+|basic\s+|token\s+)?[^\s\"']+"),
        r"\1[REDACTED]",
    ),
    (re.compile(r"(?i)\bbearer\s+[A-Za-z0-9._~+/=-]{8,}"), "Bearer [REDACTED]"),
    (re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{20,}"), "[REDACTED:github-token]"),
    (re.compile(r"\bgithub_pat_[A-Za-z0-9_]{20,}"), "[REDACTED:github-token]"),
    (re.compile(r"\bsk-[A-Za-z0-9_-]{16,}"), "[REDACTED:api-key]"),
    (re.compile(r"\bAKIA[0-9A-Z]{16}\b"), "[REDACTED:aws-key]"),
    (re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}"), "[REDACTED:slack-token]"),
    (re.compile(r"\bAIza[0-9A-Za-z_-]{30,}"), "[REDACTED:google-key]"),
    (
        re.compile(r"\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}"),
        "[REDACTED:jwt]",
    ),
    # scheme://user:password@host
    (re.compile(r"(?i)\b([a-z][a-z0-9+.-]*://)[^/\s:@]+:[^/\s@]+@"), r"\1[REDACTED]@"),
    # name=value / name: value / "name": "value" where the name looks secret
    (
        re.compile(
            r"(?i)\b(" + _SECRET_NAME + r")([\"']?\s*[=:]\s*)"
            r"(?:\"[^\"]*\"|'[^']*'|[^\s\"'&;]{6,})"
        ),
        r"\1\2[REDACTED]",
    ),
]

_ENV_SECRET_NAME = re.compile(
    r"(?i)(token|secret|passw(or)?d|api_?key|credential|private_?key|auth)"
)


def secret_env_values(env) -> list[str]:
    """Values of environment variables whose names look secret, longest first."""
    values = {
        v
        for k, v in env.items()
        if isinstance(v, str) and len(v) >= 8 and _ENV_SECRET_NAME.search(k)
    }
    return sorted(values, key=len, reverse=True)


def redact(text: str, env=None) -> str:
    """Best-effort removal of credentials from free text.

    Covers private-key blocks, Authorization headers, bearer tokens, common
    token prefixes (GitHub, OpenAI/Anthropic-style sk-, AWS, Slack, Google),
    JWTs, URL credentials, `secret_name=value` pairs, and the literal values
    of secret-looking environment variables. It will miss things, and it will
    occasionally over-redact (for example `token: abcdefg`). Treat the log as
    sensitive anyway.
    """
    if env is not None:
        for value in secret_env_values(env):
            text = text.replace(value, "[REDACTED:env]")
    for pattern, repl in _PATTERNS:
        text = pattern.sub(repl, text)
    return text


# --------------------------------------------------------------------------
# Text helpers
# --------------------------------------------------------------------------


def clip(text: str, limit: int) -> str:
    """Keep the head and the tail. Errors usually end with the useful line."""
    if len(text) <= limit:
        return text
    head = int(limit * 0.6)
    tail = limit - head
    omitted = len(text) - limit
    return f"{text[:head]}\n...[{omitted} chars omitted]...\n{text[-tail:]}"


def scrub(text: str, limit: int, env=None) -> str:
    return clip(redact(clip(text, PRE_CLIP), env), limit)


_HEX = re.compile(r"0x[0-9a-f]+|\b[0-9a-f]{7,}\b")
_PATH = re.compile(r"(?:[a-z]:)?(?:[\\/][\w.@+~-]+){2,}")
_NUM = re.compile(r"\d+")
_WS = re.compile(r"\s+")


def signature(tool: str, error_text: str) -> str:
    """Stable short hash for 'the same failure' across runs and machines."""
    lines = [ln.strip() for ln in error_text.splitlines() if ln.strip()][:5]
    norm = " | ".join(lines).lower()
    norm = _HEX.sub("<hex>", norm)
    norm = _PATH.sub("<path>", norm)
    norm = _NUM.sub("<n>", norm)
    norm = _WS.sub(" ", norm)
    return hashlib.sha1(f"{tool}\n{norm}".encode("utf-8", "replace")).hexdigest()[:16]


# --------------------------------------------------------------------------
# Projects and opt-out
# --------------------------------------------------------------------------


def find_project(start: str) -> tuple[str | None, bool]:
    """Walk up from `start`. Returns (project_root, ignored).

    project_root is the nearest directory containing `.git` (a directory or a
    worktree file), or None. ignored is True if an ignore marker exists in
    `start` or any parent, including parents above the project root, so one
    marker can cover a whole folder of repositories.
    """
    root: str | None = None
    current = os.path.abspath(start)
    while True:
        if os.path.exists(os.path.join(current, IGNORE_MARKER)):
            return root, True
        if root is None and os.path.exists(os.path.join(current, ".git")):
            root = current
        parent = os.path.dirname(current)
        if parent == current:
            return root, False
        current = parent


def project_name(root: str | None, start: str) -> str | None:
    base = os.path.basename(os.path.normpath(root or start))
    return base or None


def is_disabled(env) -> bool:
    return str(env.get("FAILCAP_DISABLE", "")).strip().lower() not in ("", "0", "false", "no")


# --------------------------------------------------------------------------
# Payload -> record
# --------------------------------------------------------------------------

_EXIT_RE = re.compile(r"^\s*Exit code (-?\d+)")
_EXIT_KEYS = ("exit_code", "exitCode", "returncode", "returnCode", "code")
_TEXT_KEYS = ("error", "stderr", "stdout", "output", "content", "text", "message")


def _text_of(value) -> str:
    """Flatten a tool response (str / dict / list) to plain text."""
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        parts = [_text_of(value[k]) for k in _TEXT_KEYS if k in value]
        return "\n".join(p for p in parts if p)
    if isinstance(value, list):
        return "\n".join(p for p in (_text_of(v) for v in value) if p)
    return str(value)


def _exit_code(error_text: str, response) -> int | None:
    m = _EXIT_RE.match(error_text)
    if m:
        return int(m.group(1))
    if isinstance(response, dict):
        for key in _EXIT_KEYS:
            v = response.get(key)
            if isinstance(v, int) and not isinstance(v, bool):
                return v
    return None


def summarize_input(tool: str, tool_input, env=None) -> dict:
    if not isinstance(tool_input, dict):
        return {"value": scrub(str(tool_input), 300, env)}
    out: dict = {}
    for key, value in list(tool_input.items())[:12]:
        if isinstance(value, str):
            limit = 1000 if (tool == "Bash" and key == "command") else 300
            out[key] = scrub(value, limit, env)
        elif value is None or isinstance(value, (bool, int, float)):
            out[key] = value
        else:
            out[key] = scrub(json.dumps(value, default=str), 300, env)
    return out


def build_record(payload: dict, env=None, project: str | None = None) -> dict | None:
    """Return a log record for a failed call, or None when there is nothing to log."""
    event = payload.get("hook_event_name")
    tool = payload.get("tool_name")
    if not isinstance(tool, str) or not tool:
        return None

    response = payload.get("tool_response")
    error_raw = payload.get("error")
    error_text = error_raw if isinstance(error_raw, str) else _text_of(error_raw)
    if not error_text:
        error_text = _text_of(response)

    if event == "PostToolUseFailure":
        if payload.get("is_interrupt"):
            return None  # the user pressed Esc; that is not a failure to learn from
        exit_code = _exit_code(error_text, response)
    elif event == "PostToolUse" and tool == "Bash":
        exit_code = _exit_code(error_text, response)
        if not exit_code:  # None (unknown) or 0 (success)
            return None
    else:
        return None

    safe_error = scrub(error_text, MAX_TEXT, env)
    record = {
        "v": SCHEMA_VERSION,
        "ts": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "event": event,
        "project": project,
        "session_id": payload.get("session_id"),
        "tool_use_id": payload.get("tool_use_id"),
        "tool": tool,
        "cwd": payload.get("cwd"),
        "input": summarize_input(tool, payload.get("tool_input"), env),
        "error": safe_error,
        "sig": signature(tool, safe_error),
    }
    if exit_code is not None:
        record["exit_code"] = exit_code
    return record


# --------------------------------------------------------------------------
# Log file
# --------------------------------------------------------------------------


def config_dir(env) -> str:
    return env.get("CLAUDE_CONFIG_DIR") or os.path.join(os.path.expanduser("~"), ".claude")


def log_path(env) -> str:
    return env.get("FAILCAP_LOG") or os.path.join(config_dir(env), "failcap", "failures.jsonl")


def _rotate_if_needed(path: str, env) -> None:
    try:
        limit = int(env.get("FAILCAP_MAX_BYTES", DEFAULT_MAX_BYTES))
        if os.path.getsize(path) >= limit:
            os.replace(path, path + ".1")
    except (OSError, ValueError):
        pass


def already_logged(path: str, tool_use_id: str) -> bool:
    """True when this tool_use_id is in the tail of the log (event de-duplication)."""
    try:
        with open(path, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            fh.seek(max(0, fh.tell() - DEDUPE_TAIL_BYTES))
            tail = fh.read().decode("utf-8", "replace")
    except OSError:
        return False
    return json.dumps(tool_use_id) in tail


def append(path: str, record: dict, env=None) -> None:
    env = os.environ if env is None else env
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, mode=0o700, exist_ok=True)
    _rotate_if_needed(path, env)
    line = json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
    data = line.encode("utf-8", "replace")
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o600)
    try:
        view = memoryview(data)
        while view:  # a single O_APPEND write is the norm; loop covers short writes
            view = view[os.write(fd, view) :]
    finally:
        os.close(fd)


# --------------------------------------------------------------------------
# Hook mode
# --------------------------------------------------------------------------


def main(stdin=None, env=None) -> int:
    """Hook entry point. Always returns 0: a broken logger must never get in the way."""
    env = os.environ if env is None else env
    try:
        if is_disabled(env):
            return 0
        stream = stdin if stdin is not None else sys.stdin.buffer
        raw = stream.read(MAX_STDIN)
        payload = json.loads(raw.decode("utf-8", "replace"))
        if not isinstance(payload, dict):
            return 0

        start = env.get("CLAUDE_PROJECT_DIR") or payload.get("cwd") or os.getcwd()
        root, ignored = find_project(start)
        if ignored:
            return 0

        record = build_record(payload, env, project_name(root, start))
        if record is None:
            return 0
        path = log_path(env)
        tool_use_id = record.get("tool_use_id")
        if tool_use_id and already_logged(path, tool_use_id):
            return 0
        append(path, record, env)
    except Exception as exc:  # noqa: BLE001 - deliberate: never fail the hook
        print(f"failcap: {type(exc).__name__}: {exc}", file=sys.stderr)
    return 0


# --------------------------------------------------------------------------
# Command mode: install / uninstall / status
# --------------------------------------------------------------------------


class SettingsError(Exception):
    pass


def default_settings_path(env) -> str:
    return os.path.join(config_dir(env), "settings.json")


def load_settings(path: str) -> dict:
    """Read a settings file. Missing file -> {}. Anything unreadable -> SettingsError."""
    try:
        # utf-8-sig: Windows editors and PowerShell often write a BOM
        with open(path, "r", encoding="utf-8-sig") as fh:
            text = fh.read()
    except FileNotFoundError:
        return {}
    except OSError as exc:
        raise SettingsError(f"cannot read {path}: {exc}") from exc
    if not text.strip():
        return {}
    try:
        data = json.loads(text)
    except ValueError as exc:
        raise SettingsError(f"{path} is not valid JSON ({exc}); refusing to touch it") from exc
    if not isinstance(data, dict):
        raise SettingsError(f"{path} does not contain a JSON object; refusing to touch it")
    return data


def _is_failcap_hook(hook) -> bool:
    if not isinstance(hook, dict):
        return False
    parts = [str(hook.get("command", ""))] + [str(a) for a in (hook.get("args") or [])]
    return any("failcap.py" in part for part in parts)


def _strip_failcap(groups: list) -> list:
    """Remove failcap hooks from matcher groups, keeping every other hook."""
    kept = []
    for group in groups:
        if not isinstance(group, dict) or not isinstance(group.get("hooks"), list):
            kept.append(group)
            continue
        remaining = [h for h in group["hooks"] if not _is_failcap_hook(h)]
        if remaining:
            kept.append({**group, "hooks": remaining})
        elif len(remaining) == len(group["hooks"]):
            kept.append(group)  # was already empty; not ours to remove
    return kept


def _hooks_section(data: dict) -> dict:
    hooks = data.get("hooks")
    if hooks is None:
        return {}
    if not isinstance(hooks, dict):
        raise SettingsError('"hooks" in the settings file is not an object; refusing to touch it')
    return hooks


def apply_install(data: dict, python: str, script: str) -> dict:
    hooks = _hooks_section(data)
    groups = hooks.get(HOOK_EVENT, [])
    if not isinstance(groups, list):
        raise SettingsError(f'"hooks.{HOOK_EVENT}" is not a list; refusing to touch it')
    groups = _strip_failcap(groups)
    groups.append(
        {
            "hooks": [
                {
                    "type": "command",
                    "command": python,
                    "args": [script],
                    "timeout": HOOK_TIMEOUT_SECONDS,
                }
            ]
        }
    )
    return {**data, "hooks": {**hooks, HOOK_EVENT: groups}}


def apply_uninstall(data: dict) -> dict:
    hooks = dict(_hooks_section(data))
    for event in list(hooks):
        groups = hooks[event]
        if not isinstance(groups, list):
            continue
        stripped = _strip_failcap(groups)
        if stripped:
            hooks[event] = stripped
        else:
            del hooks[event]
    out = {k: v for k, v in data.items() if k != "hooks"}
    if hooks:
        out["hooks"] = hooks
    return out


def has_failcap_hook(data: dict) -> bool:
    try:
        hooks = _hooks_section(data)
    except SettingsError:
        return False
    for groups in hooks.values():
        if not isinstance(groups, list):
            continue
        for group in groups:
            if isinstance(group, dict) and isinstance(group.get("hooks"), list):
                if any(_is_failcap_hook(h) for h in group["hooks"]):
                    return True
    return False


def write_settings(path: str, data: dict) -> None:
    """Atomic write; keeps the first backup of a pre-existing file as <path>.failcap.bak."""
    parent = os.path.dirname(os.path.abspath(path))
    os.makedirs(parent, exist_ok=True)
    backup = path + ".failcap.bak"
    if os.path.exists(path) and not os.path.exists(backup):
        with open(path, "rb") as src, open(backup, "wb") as dst:
            dst.write(src.read())
    tmp = f"{path}.failcap.{os.getpid()}.tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=2, ensure_ascii=False)
        fh.write("\n")
    os.replace(tmp, path)


def _count_records(path: str) -> int:
    count = 0
    with open(path, "rb") as fh:
        for _ in fh:
            count += 1
    return count


def _last_record(path: str) -> dict | None:
    try:
        with open(path, "rb") as fh:
            fh.seek(0, os.SEEK_END)
            fh.seek(max(0, fh.tell() - DEDUPE_TAIL_BYTES))
            lines = fh.read().decode("utf-8", "replace").splitlines()
    except OSError:
        return None
    for line in reversed(lines):
        try:
            value = json.loads(line)
        except ValueError:
            continue
        if isinstance(value, dict):
            return value
    return None


def cmd_install(args, env, out) -> int:
    path = args.settings or default_settings_path(env)
    python = args.python or sys.executable
    script = os.path.abspath(__file__)
    try:
        updated = apply_install(load_settings(path), python, script)
    except SettingsError as exc:
        print(f"failcap: {exc}", file=sys.stderr)
        return 1
    if args.dry_run:
        print(f"# would write {path}", file=out)
        print(json.dumps(updated, indent=2), file=out)
        return 0
    write_settings(path, updated)
    print(f"Installed the {HOOK_EVENT} hook in {path}", file=out)
    print(f"  runs: {python} {script}", file=out)
    print(f"  log:  {log_path(env)}", file=out)
    print("Open /hooks in Claude Code (or start a new session) to load it.", file=out)
    print("If you move this file or change Python, run install again.", file=out)
    return 0


def cmd_uninstall(args, env, out) -> int:
    path = args.settings or default_settings_path(env)
    try:
        data = load_settings(path)
        if not has_failcap_hook(data):
            print(f"No failcap hook found in {path}; nothing to do.", file=out)
            return 0
        updated = apply_uninstall(data)
    except SettingsError as exc:
        print(f"failcap: {exc}", file=sys.stderr)
        return 1
    if args.dry_run:
        print(f"# would write {path}", file=out)
        print(json.dumps(updated, indent=2), file=out)
        return 0
    write_settings(path, updated)
    print(f"Removed the failcap hook from {path}. The log was left in place.", file=out)
    return 0


def cmd_status(args, env, out) -> int:
    settings = args.settings or default_settings_path(env)
    path = log_path(env)
    try:
        installed = has_failcap_hook(load_settings(settings))
        settings_note = "hook installed" if installed else "hook NOT installed"
    except SettingsError as exc:
        settings_note = f"unreadable ({exc})"
    print(f"settings: {settings}: {settings_note}", file=out)
    if is_disabled(env):
        print("capture:  DISABLED by FAILCAP_DISABLE in this environment", file=out)
    print(f"log:      {path}", file=out)
    if not os.path.exists(path):
        print("          no log yet (no failures captured)", file=out)
        return 0
    size = os.path.getsize(path)
    print(f"          {_count_records(path)} records, {size} bytes", file=out)
    last = _last_record(path)
    if last:
        print(
            f"          last: {last.get('ts')} {last.get('project')} {last.get('tool')}",
            file=out,
        )
    return 0


def cli(argv, env=None, out=None) -> int:
    env = os.environ if env is None else env
    out = sys.stdout if out is None else out
    parser = argparse.ArgumentParser(
        prog="failcap.py",
        description="Capture failing Claude Code tool calls to a JSONL log. "
        "With no arguments it runs as the hook and reads the event on stdin.",
    )
    sub = parser.add_subparsers(dest="command", required=True)
    for name, helptext in (
        ("install", "register the hook in a Claude Code settings file"),
        ("uninstall", "remove the hook (the log is left in place)"),
        ("status", "show hook and log status"),
    ):
        p = sub.add_parser(name, help=helptext)
        p.add_argument("--settings", help="settings file to use (default: <config dir>/settings.json)")
        if name != "status":
            p.add_argument("--dry-run", action="store_true", help="print the result, write nothing")
        if name == "install":
            p.add_argument(
                "--python",
                help="interpreter the hook should use (default: the one running this command)",
            )
    args = parser.parse_args(argv)
    handler = {"install": cmd_install, "uninstall": cmd_uninstall, "status": cmd_status}[args.command]
    return handler(args, env, out)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        sys.exit(cli(sys.argv[1:]))
    if sys.stdin.isatty():
        print("failcap runs as a Claude Code hook. Try: failcap.py install | uninstall | status", file=sys.stderr)
        sys.exit(2)
    sys.exit(main())
