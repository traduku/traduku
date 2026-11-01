#!/usr/bin/env python3
"""Tiny OpenAI-compatible MOCK server for manual testing of traduku.

    ./tools/mock-openai.py [port]        # default 8931

Implements POST /v1/chat/completions - honouring "stream": true by
emitting real SSE deltas (a few per word) and GET /v1/models.
The reply echoes the request's user prompt, prefixed with the detected
destination language, e.g. "[MOCK -> ru] <first 200 chars>".

In traduku Settings:
    API address:  http://127.0.0.1:8931/v1
    Model:        test-model
    API key:      (anything)
"""
import json
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def sse(obj):
    return f"data: {json.dumps(obj)}\n\n".encode()


class Handler(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def do_GET(self):
        if self.path.rstrip("/").endswith("/models"):
            self._json({"object": "list",
                        "data": [{"id": "test-model", "object": "model"}]})
        else:
            self.send_error(404)

    def do_POST(self):
        n = int(self.headers.get("content-length", 0))
        req = json.loads(self.rfile.read(n) or b"{}")
        user = req.get("messages", [{}])[-1].get("content", "")
        m = re.search(r"into (.+?)\.", user)
        lang = m.group(1) if m else "?"
        reply = f"[MOCK -> {lang}] " + user[user.find("<<<TEXT") + 7:][:200]
        if not req.get("stream"):
            self._json({
                "id": "mock", "object": "chat.completion", "created": 0,
                "model": req.get("model", "?"),
                "choices": [{"index": 0, "finish_reason": "stop",
                             "message": {"role": "assistant",
                                         "content": reply}}]})
            return
        # streaming: word-by-word deltas, 40ms apart
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        for word in re.findall(r"\S+\s*", reply):
            self.wfile.write(sse({"id": "mock", "object":
                                  "chat.completion.chunk",
                                  "choices": [{"index": 0, "delta":
                                               {"content": word}}]}))
            self.wfile.flush()
            time.sleep(0.04)
        self.wfile.write(sse({"id": "mock", "choices":
                              [{"index": 0, "delta": {},
                                "finish_reason": "stop"}]}))
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

    def _json(self, obj):
        b = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8931
    print(f"mock OpenAI endpoint (streaming) on http://127.0.0.1:{port}/v1")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
