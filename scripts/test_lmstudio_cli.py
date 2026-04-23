#!/usr/bin/env python3
"""CLI smoke test for LM Studio OpenAI-compatible endpoints.

Checks:
1. GET /v1/models
2. POST /v1/chat/completions with stream=false
3. POST /v1/chat/completions with stream=true (SSE)

This script is intentionally dependency-free and uses urllib only.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request
from typing import Any


URL_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def http_json(url: str, method: str = "GET", payload: dict[str, Any] | None = None, timeout: int = 60) -> dict[str, Any]:
    data = None
    headers = {"content-type": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with URL_OPENER.open(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def extract_message_text(message: dict[str, Any]) -> str:
    content = message.get("content")
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts: list[str] = []
        for item in content:
            if not isinstance(item, dict):
                continue
            if item.get("type") == "text" and isinstance(item.get("text"), str):
                parts.append(item["text"])
        return "\n".join(parts)
    return ""


def extract_sync_text(payload: dict[str, Any]) -> str:
    choices = payload.get("choices")
    if not isinstance(choices, list) or not choices:
        return ""
    first = choices[0]
    if not isinstance(first, dict):
        return ""
    message = first.get("message")
    if not isinstance(message, dict):
        return ""
    return extract_message_text(message)


def stream_chat(base_url: str, body: dict[str, Any], timeout: int, dump_events: bool) -> tuple[str, str, int]:
    req = urllib.request.Request(
        f"{base_url.rstrip('/')}/v1/chat/completions",
        data=json.dumps(body).encode("utf-8"),
        headers={"content-type": "application/json"},
        method="POST",
    )
    chunks: list[str] = []
    reasoning_chunks: list[str] = []
    event_count = 0
    with URL_OPENER.open(req, timeout=timeout) as resp:
        for raw in resp:
            line = raw.decode("utf-8", errors="replace").strip()
            if not line or not line.startswith("data:"):
                continue
            payload = line[5:].strip()
            if dump_events:
                print(f"[sse] {payload}")
            if payload == "[DONE]":
                break
            event_count += 1
            try:
                event = json.loads(payload)
            except json.JSONDecodeError:
                continue
            choices = event.get("choices")
            if not isinstance(choices, list) or not choices:
                continue
            choice = choices[0]
            if not isinstance(choice, dict):
                continue
            delta = choice.get("delta")
            if isinstance(delta, dict):
                if isinstance(delta.get("reasoning_content"), str):
                    reasoning_chunks.append(delta["reasoning_content"])
                if isinstance(delta.get("content"), str):
                    chunks.append(delta["content"])
                    continue
            message = choice.get("message")
            if isinstance(message, dict):
                text = extract_message_text(message)
                if text:
                    chunks.append(text)
    return "".join(chunks), "".join(reasoning_chunks), event_count


def choose_model(base_url: str, requested_model: str | None, timeout: int) -> str:
    models_payload = http_json(f"{base_url.rstrip('/')}/v1/models", timeout=timeout)
    model_entries = models_payload.get("data")
    if not isinstance(model_entries, list) or not model_entries:
        raise RuntimeError("LM Studio returned no models in /v1/models")

    model_ids = []
    for entry in model_entries:
        if isinstance(entry, dict) and isinstance(entry.get("id"), str) and entry["id"]:
            model_ids.append(entry["id"])

    if not model_ids:
        raise RuntimeError("No model ids found in /v1/models")

    if requested_model:
        if requested_model not in model_ids:
            raise RuntimeError(f"Requested model '{requested_model}' not found. Available: {', '.join(model_ids)}")
        return requested_model
    return model_ids[0]


def main() -> int:
    parser = argparse.ArgumentParser(description="LM Studio CLI smoke test")
    parser.add_argument("--base-url", default="http://127.0.0.1:1234")
    parser.add_argument("--model")
    parser.add_argument("--prompt", default="Responda apenas com OK.")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--dump-events", action="store_true")
    args = parser.parse_args()

    try:
        for proxy_var in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "all_proxy"):
            if proxy_var in os.environ:
                print(f"[info] ignoring env proxy: {proxy_var}")
        started = time.time()
        model = choose_model(args.base_url, args.model, args.timeout)
        print(f"[ok] model selected: {model}")

        models_payload = http_json(f"{args.base_url.rstrip('/')}/v1/models", timeout=args.timeout)
        print(f"[ok] /v1/models returned {len(models_payload.get('data', []))} model(s)")

        sync_body = {
            "model": model,
            "stream": False,
            "temperature": 0.0,
            "max_tokens": 128,
            "messages": [
                {"role": "system", "content": "Seja conciso."},
                {"role": "user", "content": args.prompt},
            ],
        }
        sync_payload = http_json(f"{args.base_url.rstrip('/')}/v1/chat/completions", method="POST", payload=sync_body, timeout=args.timeout)
        sync_text = extract_sync_text(sync_payload)
        print(f"[ok] sync response length: {len(sync_text)}")
        if sync_text:
            print(f"[sync] {sync_text[:300]}")
        else:
            print("[warn] sync response came back empty")

        stream_body = {
            "model": model,
            "stream": True,
            "temperature": 0.0,
            "max_tokens": 128,
            "messages": [
                {"role": "system", "content": "Seja conciso."},
                {"role": "user", "content": args.prompt},
            ],
        }
        stream_text, reasoning_text, event_count = stream_chat(args.base_url, stream_body, args.timeout, args.dump_events)
        print(f"[ok] stream events: {event_count}")
        print(f"[ok] reasoning length: {len(reasoning_text)}")
        print(f"[ok] stream response length: {len(stream_text)}")
        if reasoning_text:
            print(f"[reasoning] {reasoning_text[:300]}")
        if stream_text:
            print(f"[stream] {stream_text[:300]}")
        else:
            print("[warn] stream response came back empty")

        print(f"[done] elapsed: {time.time() - started:.2f}s")
        return 0
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        print(f"[error] HTTP {exc.code}: {body}", file=sys.stderr)
        return 1
    except Exception as exc:  # noqa: BLE001
        print(f"[error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
