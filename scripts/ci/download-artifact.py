#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""通过 Gitea REST API 下载当前运行的候选文件，不依赖外部下载 action。"""
import argparse
import base64
import io
import json
import os
from pathlib import Path
import sys
import urllib.error
import urllib.parse
import urllib.request
import zipfile


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def get(url, authorization):
    origin = urllib.parse.urlsplit(url).netloc
    opener = urllib.request.build_opener(NoRedirect)
    headers = {"Authorization": authorization} if authorization else {}
    for _ in range(4):
        try:
            with opener.open(urllib.request.Request(url, headers=headers), timeout=180) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            if error.code not in (301, 302, 303, 307, 308):
                raise ValueError(f"Artifact request failed: HTTP {error.code}") from None
            url = urllib.parse.urljoin(url, error.headers.get("Location", ""))
            if urllib.parse.urlsplit(url).scheme != "https":
                raise ValueError("Artifact redirect must use HTTPS")
            # Gitea 同源 /zip/raw 需要认证；跨源对象存储不接收认证头。
            if urllib.parse.urlsplit(url).netloc != origin:
                headers = {}
        except urllib.error.URLError:
            raise ValueError("Artifact request transport failed") from None
    raise ValueError("Too many artifact redirects")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    secret = os.environ["USER_PASSWORD"]
    auth = "Basic " + base64.b64encode(f"bismarckkk:{secret}".encode()).decode()
    repository = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    sha = os.environ["GITHUB_SHA"]
    base = "https://git.in.encos.cn/api/v1/repos/" + repository
    payload = json.loads(get(f"{base}/actions/runs/{run_id}/artifacts", auth))
    matches = [a for a in payload["artifacts"] if a["name"] == args.name]
    if len(matches) != 1:
        raise ValueError("Expected exactly one candidate artifact for this run")
    artifact = matches[0]
    if artifact.get("expired") or artifact["workflow_run"]["head_sha"] != sha:
        raise ValueError("Artifact expired or producer commit does not match")
    archive = get(f"{base}/actions/artifacts/{int(artifact['id'])}/zip", auth)
    args.destination.mkdir(parents=True, exist_ok=True)
    if any(args.destination.iterdir()):
        raise ValueError("Artifact destination must be empty")
    with zipfile.ZipFile(io.BytesIO(archive)) as package:
        names = set()
        for member in package.infolist():
            name = member.filename
            if (not name or name in (".", "..") or "/" in name or "\\" in name
                    or name in names or member.is_dir()
                    or (member.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError("Candidate artifact must contain unique flat regular files")
            names.add(name)
        for member in package.infolist():
            (args.destination / member.filename).write_bytes(package.read(member))
    print(f"Downloaded {args.name}: {len(names)} files from this workflow run")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        print(f"Artifact download aborted: {error}", file=sys.stderr)
        sys.exit(1)
