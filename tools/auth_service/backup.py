#!/usr/bin/env python3
"""Daily backup of auth.db.

1) local copy: ~/lumenarc-auth/backup/auth-<date>.db (keep last 14), WAL-safe via
   the sqlite3 backup API;
2) offsite copy to Tencent COS (object ACL private) when ~/lumenarc-auth/cos.json
   exists — protects against the pavo disk dying or the box itself being lost.

cos.json: {"secret_id": "...", "secret_key": "...", "bucket": "...", "region": "..."}
"""
import glob
import hashlib
import hmac
import json
import os
import sqlite3
import sys
import time
import urllib.parse

home = os.path.expanduser("~/lumenarc-auth")
src = os.path.join(home, "data", "auth.db")
out_dir = os.path.join(home, "backup")
os.makedirs(out_dir, exist_ok=True)
stamp = time.strftime("%Y%m%d")
dst = os.path.join(out_dir, "auth-%s.db" % stamp)

con = sqlite3.connect(src)
bak = sqlite3.connect(dst)
with bak:
    con.backup(bak)
bak.close()
con.close()

files = sorted(glob.glob(os.path.join(out_dir, "auth-*.db")))
for f in files[:-14]:
    os.remove(f)
print(time.strftime("%F %T"), "local backup ->", dst, os.path.getsize(dst), "bytes", flush=True)


def sha1_hex(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def hmac_sha1_hex(key: bytes, msg: str) -> str:
    return hmac.new(key, msg.encode(), hashlib.sha1).hexdigest()


def upload_cos(path: str, key: str) -> None:
    import requests

    cfg_path = os.path.join(home, "cos.json")
    if not os.path.exists(cfg_path):
        print("cos.json 不存在，跳过异地备份", flush=True)
        return
    cfg = json.load(open(cfg_path, encoding="utf-8"))
    sid, skey = cfg["secret_id"], cfg["secret_key"]
    bucket, region = cfg["bucket"], cfg["region"]
    host = "%s.cos.%s.myqcloud.com" % (bucket, region)
    now = int(time.time())
    key_time = "%d;%d" % (now, now + 3600)
    sign_key = hmac_sha1_hex(skey.encode(), key_time)
    http_string = "put\n/%s\n\nhost=%s\n" % (key, host)
    string_to_sign = "sha1\n%s\n%s\n" % (key_time, sha1_hex(http_string.encode()))
    signature = hmac_sha1_hex(sign_key.encode(), string_to_sign)
    auth = ("q-sign-algorithm=sha1&q-ak=%s&q-sign-time=%s&q-key-time=%s"
            "&q-header-list=host&q-url-param-list=&q-signature=%s"
            % (sid, key_time, key_time, signature))
    url = "https://%s/%s" % (host, urllib.parse.quote(key))
    with open(path, "rb") as f:
        body = f.read()
    r = requests.put(url, data=body, timeout=60,
                     headers={"Host": host, "Authorization": auth,
                              "x-cos-acl": "private",
                              "Content-Type": "application/octet-stream"})
    print(time.strftime("%F %T"), "cos upload", key, r.status_code, flush=True)
    if r.status_code // 100 != 2:
        print("cos error body:", r.text[:300], flush=True)


try:
    upload_cos(dst, "lumenarc-auth/auth-%s.db" % stamp)
except Exception as e:  # 异地失败不影响本地备份
    print("cos upload failed:", e, flush=True)
    sys.exit(0)
