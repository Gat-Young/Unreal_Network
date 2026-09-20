#!/usr/bin/env python3
"""언리얼 서버 IP 레지스트리 웹서버 (파이썬 표준 라이브러리만 사용).

- POST /api/login               {username, password} -> {token}   (없는 계정은 최초 로그인 시 자동 가입)
- POST /api/servers/register    (X-Server-Key 필요) 서버 등록/하트비트
- POST /api/servers/unregister  (X-Server-Key 필요) 서버 등록 해제
- GET  /api/servers             (Authorization: Bearer <token> 필요) 등록된 서버 목록
- GET  /health
환경변수: REGISTRY_HOST(기본 0.0.0.0) REGISTRY_PORT(기본 8080) REGISTRY_SERVER_KEY(기본 dev-server-key) REGISTRY_TTL(기본 30초)
"""
import hashlib
import hmac
import json
import os
import secrets
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = os.environ.get("REGISTRY_HOST", "0.0.0.0")
PORT = int(os.environ.get("REGISTRY_PORT", "8080"))
SERVER_KEY = os.environ.get("REGISTRY_SERVER_KEY", "dev-server-key")
TTL = float(os.environ.get("REGISTRY_TTL", "30"))
USERS_FILE = os.environ.get("REGISTRY_USERS_FILE", os.path.join(os.path.dirname(os.path.abspath(__file__)), "users.json"))

lock = threading.Lock()
servers = {}   # "ip:port" -> info
tokens = {}    # token -> (username, expires)
TOKEN_LIFETIME = 3600 * 12


def load_users():
    try:
        with open(USERS_FILE, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def hash_pw(password, salt):
    return hashlib.pbkdf2_hmac("sha256", password.encode(), bytes.fromhex(salt), 100_000).hex()


def local_lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        print("[%s] %s" % (self.log_date_time_string(), fmt % args), flush=True)

    def _send(self, code, obj):
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0:
            return {}
        try:
            data = json.loads(self.rfile.read(n).decode("utf-8"))
            return data if isinstance(data, dict) else {}
        except ValueError:
            return {}

    def _server_authed(self):
        return hmac.compare_digest(self.headers.get("X-Server-Key", ""), SERVER_KEY)

    def _user(self):
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return None
        with lock:
            entry = tokens.get(auth[7:])
            if entry and entry[1] > time.time():
                return entry[0]
        return None

    def do_GET(self):
        if self.path == "/health":
            return self._send(200, {"ok": True})
        if self.path == "/api/servers":
            if not self._user():
                return self._send(401, {"error": "login required"})
            now = time.time()
            with lock:
                for k in [k for k, v in servers.items() if v["expires"] < now]:
                    del servers[k]
                out = [{k: v for k, v in s.items() if k != "expires"} for s in servers.values()]
            return self._send(200, {"servers": out})
        self._send(404, {"error": "not found"})

    def do_POST(self):
        data = self._body()
        if self.path == "/api/login":
            user, pw = str(data.get("username", "")).strip(), str(data.get("password", ""))
            if not user or not pw or len(user) > 32:
                return self._send(400, {"error": "username/password required"})
            with lock:
                users = load_users()
                rec = users.get(user)
                if rec is None:
                    salt = secrets.token_hex(16)
                    users[user] = {"salt": salt, "hash": hash_pw(pw, salt)}
                    with open(USERS_FILE, "w", encoding="utf-8") as f:
                        json.dump(users, f)
                elif not hmac.compare_digest(rec["hash"], hash_pw(pw, rec["salt"])):
                    return self._send(401, {"error": "invalid credentials"})
                token = secrets.token_urlsafe(24)
                tokens[token] = (user, time.time() + TOKEN_LIFETIME)
            return self._send(200, {"token": token, "username": user})

        if self.path in ("/api/servers/register", "/api/servers/unregister"):
            if not self._server_authed():
                return self._send(403, {"error": "bad server key"})
            ip = str(data.get("ip") or "").strip() or self.client_address[0]
            if ip in ("127.0.0.1", "::1") and not data.get("ip"):
                ip = local_lan_ip()
            try:
                port = int(data.get("port", 7777))
            except (TypeError, ValueError):
                return self._send(400, {"error": "bad port"})
            key = "%s:%d" % (ip, port)
            with lock:
                if self.path.endswith("unregister"):
                    servers.pop(key, None)
                else:
                    servers[key] = {
                        "name": str(data.get("name", "Unreal Server"))[:64], "ip": ip, "port": port,
                        "map": str(data.get("map", ""))[:128], "players": int(data.get("players", 0)),
                        "maxPlayers": int(data.get("maxPlayers", 0)), "expires": time.time() + TTL,
                    }
            print("[registry] %s %s" % (self.path.rsplit("/", 1)[1], key), flush=True)
            return self._send(200, {"ok": True, "address": key})
        self._send(404, {"error": "not found"})


if __name__ == "__main__":
    print("Registry listening on %s:%d (server key: %s)" % (HOST, PORT, "custom" if "REGISTRY_SERVER_KEY" in os.environ else "dev-server-key"), flush=True)
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
