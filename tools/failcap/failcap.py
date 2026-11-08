#!/usr/bin/env python3
"""failcap: record failing Claude Code tool calls to a JSONL log.

Runs as a Claude Code hook. Reads the hook payload (JSON) on stdin and, when
the call failed, appends one redacted record to a JSONL file. It is
deliberately boring: standard library only, never blocks the tool call, never
raises, and always exits 0.

Events handled
  PostToolUseFailure   any tool that failed. Bash non-zero exits arrive here
                       (verified in a live session), so this is the only event
                       .claude/settings.json registers.
  PostToolUse          only Bash, and only when the response shows a non-zero
                       exit code. Not registered by default; kept as a
                       fallback in case a Claude Code version reports Bash
                       failures through PostToolUse instead.

Log location (first match wins)
  $FAILCAP_LOG
  $CLAUDE_PROJECT_DIR/.claude/failcap/failures.jsonl
  <payload cwd>/.claude/failcap/failures.jsonl

Environment knobs
  FAILCAP_LOG        path of the JSONL file
  FAILCAP_MAX_BYTES  rotate to <log>.1 once the file is this big (default 20 MB)

Record schema (v1), one JSON object per line:
  v, ts, event, session_id, tool_use_id, tool, cwd, input, error,
  exit_code (when known), sig
`sig` is a short hash of the error text with paths, numbers and hashes
normalised, so the same failure on different machines or runs groups together.

Redaction is best effort, not a guarantee. See redact().
"""

from __future__ import annotations

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
    # name=value / name: value where the name looks secret
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


def build_record(payload: dict, env=None) -> dict | None:
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


def log_path(env, payload: dict) -> str:
    explicit = env.get("FAILCAP_LOG")
    if explicit:
        return explicit
    base = env.get("CLAUDE_PROJECT_DIR") or payload.get("cwd") or os.getcwd()
    return os.path.join(base, ".claude", "failcap", "failures.jsonl")


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
# Entry point
# --------------------------------------------------------------------------


def main(stdin=None, env=None) -> int:
    """Always returns 0. A broken logger must never get in the way of a tool call."""
    env = os.environ if env is None else env
    try:
        stream = stdin if stdin is not None else sys.stdin.buffer
        raw = stream.read(MAX_STDIN)
        payload = json.loads(raw.decode("utf-8", "replace"))
        if not isinstance(payload, dict):
            return 0
        record = build_record(payload, env)
        if record is None:
            return 0
        path = log_path(env, payload)
        tool_use_id = record.get("tool_use_id")
        if tool_use_id and already_logged(path, tool_use_id):
            return 0
        append(path, record, env)
    except Exception as exc:  # noqa: BLE001 - deliberate: never fail the hook
        print(f"failcap: {type(exc).__name__}: {exc}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
