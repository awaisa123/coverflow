"""
Cover Flow library sync - fills Cover Flow with the games installed on your PS5.

Cover Flow runs in the PS5's app sandbox and cannot read the installed-games
list itself. Your HEN's FTP server can, so this script (run on your PC):
  1. reads the installed titles from the PS5's app metadata over FTP,
  2. takes each one's name (param.json / param.sfo) and icon (icon0.png),
  3. uploads the icons and a games.txt to Cover Flow's covers folder.

Everything goes from the PS5 to your PC and straight back; nothing is sent
anywhere else.

Usage (Windows):  py ps5_library_sync.py 192.168.1.50
Options:          --port 2121   --title PPSA99123   --dry-run
"""

import argparse
import ftplib
import io
import json
import re
import struct
import sys

TITLE_ID = re.compile(r"^[A-Z]{4}\d{5}$")
META_DIRS = ["/user/appmeta", "/system_data/priv/appmeta"]


def name_from_param_json(data: bytes) -> str:
    try:
        j = json.loads(data.decode("utf-8", "replace"))
    except ValueError:
        return ""
    loc = j.get("localizedParameters", {}) or {}
    for lang in ("en-US", "en-GB", loc.get("defaultLanguage", "")):
        name = (loc.get(lang) or {}).get("titleName", "") if lang else ""
        if name:
            return name
    for value in loc.values():
        if isinstance(value, dict) and value.get("titleName"):
            return value["titleName"]
    return j.get("titleName", "")


def name_from_sfo(d: bytes) -> str:
    if len(d) < 0x14 or d[:4] != b"\0PSF":
        return ""
    keys, data, count = struct.unpack_from("<III", d, 0x08)
    for i in range(min(count, 256)):
        e = 0x14 + i * 16
        if e + 16 > len(d):
            break
        key_off, _fmt, length, _max, data_off = struct.unpack_from("<HHIII", d, e)
        k = d[keys + key_off:].split(b"\0", 1)[0]
        if k == b"TITLE":
            v = d[data + data_off:data + data_off + length]
            return v.split(b"\0", 1)[0].decode("utf-8", "replace")
    return ""


def fetch(ftp: ftplib.FTP, path: str) -> bytes | None:
    buf = io.BytesIO()
    try:
        ftp.retrbinary("RETR " + path, buf.write)
    except ftplib.all_errors:
        return None
    return buf.getvalue()


def list_names(ftp: ftplib.FTP, path: str) -> list[str]:
    try:
        names = ftp.nlst(path)
    except ftplib.all_errors:
        return []
    return [n.rstrip("/").split("/")[-1] for n in names]


def main() -> int:
    ap = argparse.ArgumentParser(description="Fill Cover Flow with the games installed on your PS5")
    ap.add_argument("host", help="the PS5's IP address")
    ap.add_argument("--port", type=int, default=2121, help="FTP port (ftpsrv: 2121)")
    ap.add_argument("--title", default="PPSA99123", help="Cover Flow's title id")
    ap.add_argument("--dry-run", action="store_true", help="list what would be written, upload nothing")
    args = ap.parse_args()

    ftp = ftplib.FTP()
    print(f"Connecting to {args.host}:{args.port} ...")
    ftp.connect(args.host, args.port, timeout=15)
    ftp.login()
    ftp.set_pasv(True)

    games, seen = [], set()
    for meta in META_DIRS:
        ids = [n for n in list_names(ftp, meta) if TITLE_ID.match(n)]
        print(f"{meta}: {len(ids)} title folders")
        for tid in sorted(ids):
            if tid in seen or tid == args.title or tid.startswith("NPXS"):
                continue  # Cover Flow itself and system apps
            name = ""
            raw = fetch(ftp, f"{meta}/{tid}/param.json")
            if raw:
                name = name_from_param_json(raw)
            if not name:
                raw = fetch(ftp, f"{meta}/{tid}/param.sfo")
                if raw:
                    name = name_from_sfo(raw)
            if not name:
                continue
            icon = fetch(ftp, f"{meta}/{tid}/icon0.png")
            seen.add(tid)
            games.append((tid, name, icon))
            print(f"  {tid}  {name}{'' if icon else '  (no icon)'}")

    if not games:
        print("No installed games found. Is the FTP server running with full access (from your HEN)?")
        return 1
    games.sort(key=lambda g: g[1].lower())

    covers = f"/data/homebrew/{args.title}/covers"
    lines = ["# Made by ps5_library_sync.py: image | title | subtitle | title id"]
    for tid, name, icon in games:
        kind = "PS5" if tid.startswith("PPSA") else "PS4" if tid.startswith("CUSA") else ""
        image = f"{tid}.png" if icon else ""
        lines.append(f"{image} | {name.replace('|', '/')} | {kind} | {tid}")
    text = ("\n".join(lines) + "\n").encode("utf-8")

    if args.dry_run:
        print("\n--- games.txt (dry run, nothing uploaded) ---")
        print(text.decode())
        return 0

    try:
        ftp.mkd(covers)
    except ftplib.all_errors:
        pass  # already there
    for tid, _name, icon in games:
        if icon:
            ftp.storbinary(f"STOR {covers}/{tid}.png", io.BytesIO(icon))
    ftp.storbinary(f"STOR {covers}/games.txt", io.BytesIO(text))
    ftp.quit()
    print(f"\nDone: {len(games)} games written to {covers}. Restart Cover Flow.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
