#!/usr/bin/env python3
"""Configures the test Jellyfin through its API (idempotent): startup wizard, user "playfin", the music library,
the M3U tuner and the XMLTV guide, the recordings folder, a playlist. Usage: setup.py http://127.0.0.1:8097"""
import json
import sys
import time
import urllib.error
import urllib.request

BASE = sys.argv[1].rstrip("/") if len(sys.argv) > 1 else "http://127.0.0.1:8097"
USER = "playfin"
PASSWORD = "playfin-test"  # local test server only (docs/testing.md)
CLIENT = 'MediaBrowser Client="Playfin test setup", Device="setup", DeviceId="playfin-setup", Version="1.0"'
EXPECTED_TRACKS = 30
token = None


UNAUTHORIZED = object()


def call(method, path, body=None, ok_missing=False, ok_unauthorized=False):
    headers = {"Authorization": CLIENT + (f', Token="{token}"' if token else "")}
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    for _ in range(120):  # 503 while the server is still starting
        request = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                text = response.read().decode()
                return json.loads(text) if text.strip() else None
        except urllib.error.HTTPError as error:
            if error.code == 503:
                time.sleep(1)
                continue
            if ok_missing and error.code == 404:
                return None
            if ok_unauthorized and error.code == 401:
                return UNAUTHORIZED
            raise SystemExit(f"{method} {path}: HTTP {error.code} {error.read()[:300]!r}")
    raise SystemExit(f"{method} {path}: the server stayed unavailable")


def wait_for_server():
    for _ in range(120):
        try:
            return call("GET", "/System/Info/Public")
        except (urllib.error.URLError, ConnectionError, SystemExit):
            time.sleep(1)
    raise SystemExit("the server did not answer")


def wizard():
    info = wait_for_server()
    if info.get("StartupWizardCompleted"):
        return
    # while starting, the server may report the wizard as not completed; its endpoints answer 401 once it is
    if call("GET", "/Startup/Configuration", ok_unauthorized=True) is UNAUTHORIZED:
        return
    call("POST", "/Startup/Configuration",
         {"UICulture": "fr-FR", "MetadataCountryCode": "FR", "PreferredMetadataLanguage": "fr"})
    call("GET", "/Startup/User")
    call("POST", "/Startup/User", {"Name": USER, "Password": PASSWORD})
    call("POST", "/Startup/RemoteAccess", {"EnableRemoteAccess": True, "EnableAutomaticPortMapping": False})
    call("POST", "/Startup/Complete")
    print("wizard completed")


def sign_in():
    global token
    for _ in range(30):
        try:
            auth = call("POST", "/Users/AuthenticateByName", {"Username": USER, "Pw": PASSWORD})
            token = auth["AccessToken"]
            return auth["User"]["Id"]
        except SystemExit:
            time.sleep(1)
    raise SystemExit("cannot sign in as " + USER)


def server_and_user(user_id):
    config = call("GET", "/System/Configuration")
    if config.get("ServerName") != "Playfin Test":
        config["ServerName"] = "Playfin Test"
        call("POST", "/System/Configuration", config)
        print("server named Playfin Test")
    user = call("GET", f"/Users/{user_id}")
    policy = user["Policy"]
    if policy.get("IsHidden", True):
        policy["IsHidden"] = False  # listed on the sign-in screen (profiles)
        call("POST", f"/Users/{user_id}/Policy", policy)
        print("user shown on the sign-in screen")


def music_library():
    folders = call("GET", "/Library/VirtualFolders") or []
    if any(f.get("CollectionType") == "music" for f in folders):
        return
    call("POST", "/Library/VirtualFolders?name=Musique&collectionType=music&refreshLibrary=true",
         {"LibraryOptions": {"PathInfos": [{"Path": "/media/music"}], "EnableRealtimeMonitor": False}})
    print("music library added")


def films_library():
    folders = call("GET", "/Library/VirtualFolders") or []
    if any(f.get("CollectionType") == "movies" for f in folders):
        return
    call("POST", "/Library/VirtualFolders?name=Films&collectionType=movies&refreshLibrary=true",
         {"LibraryOptions": {"PathInfos": [{"Path": "/media/films"}], "EnableRealtimeMonitor": False}})
    print("films library added")


def live_tv():
    options = call("GET", "/System/Configuration/livetv")
    if not options.get("TunerHosts"):
        call("POST", "/LiveTv/TunerHosts",
             {"Type": "m3u", "Url": "/media/livetv/channels.m3u", "FriendlyName": "Playfin test",
              "ImportFavoritesOnly": False, "AllowHWTranscoding": False, "EnableStreamLooping": False,
              "TunerCount": 0, "Source": "", "UserAgent": ""})
        print("tuner added")
    if not options.get("ListingProviders"):
        call("POST", "/LiveTv/ListingProviders?validateListings=false&validateLogin=false",
             {"Type": "xmltv", "Path": "/media/livetv/guide.xml", "EnableAllTuners": True})
        print("guide added")
    options = call("GET", "/System/Configuration/livetv")
    if options.get("RecordingPath") != "/recordings":
        options["RecordingPath"] = "/recordings"
        call("POST", "/System/Configuration/livetv", options)
        print("recordings in /recordings")


def run_task(key):
    for task in call("GET", "/ScheduledTasks") or []:
        if task.get("Key") == key:
            call("POST", f"/ScheduledTasks/Running/{task['Id']}")
            return


def wait_for_tracks(user_id):
    for _ in range(180):
        page = call("GET", f"/Items?userId={user_id}&includeItemTypes=Audio&recursive=true&limit=1")
        if page and page.get("TotalRecordCount", 0) >= EXPECTED_TRACKS:
            return
        time.sleep(1)
    raise SystemExit("the music library was not scanned in time")


def playlist(user_id):
    existing = call("GET", f"/Items?userId={user_id}&includeItemTypes=Playlist&recursive=true")
    if existing and existing.get("TotalRecordCount", 0) > 0:
        return
    tracks = call("GET", f"/Items?userId={user_id}&includeItemTypes=Audio&recursive=true&sortBy=Random&limit=8")
    ids = [item["Id"] for item in tracks["Items"]]
    call("POST", "/Playlists", {"Name": "Soirée tests", "Ids": ids, "UserId": user_id, "MediaType": "Audio"})
    print("playlist added")


def main():
    wizard()
    user_id = sign_in()
    server_and_user(user_id)
    music_library()
    films_library()
    live_tv()
    run_task("RefreshLibrary")
    run_task("RefreshGuide")
    wait_for_tracks(user_id)
    playlist(user_id)
    channels = call("GET", f"/LiveTv/Channels?userId={user_id}")
    print(f"ready: {BASE} (user {USER}), {channels.get('TotalRecordCount', 0)} channels")


main()
