"""Publish CI status and failed job logs to a Git ref in the same repository."""

import datetime
import json
import os
from pathlib import Path
import subprocess
import urllib.error
import urllib.parse
import urllib.request


ROOT = Path(os.environ["GITHUB_WORKSPACE"])
STAGE = os.environ["PAIMBNAILS_CI_STAGE"]
HEAD = os.environ["GITHUB_SHA"]
TOKEN = os.environ.get("GITHUB_TOKEN", "")


class RedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        redirected = super().redirect_request(request, response, code, message, headers, url)
        if redirected and urllib.parse.urlparse(request.full_url).netloc != urllib.parse.urlparse(url).netloc:
            redirected.remove_header("Authorization")
        return redirected


def api(path):
    request = urllib.request.Request(
        os.environ.get("GITHUB_API_URL", "https://api.github.com") + path,
        headers={"Authorization": f"Bearer {TOKEN}", "User-Agent": "Paimbnails-CI"},
    )
    with urllib.request.build_opener(RedirectHandler()).open(request, timeout=60) as response:
        return response.read()


def git(*args, data=None):
    environment = os.environ.copy()
    environment.update({
        "GIT_AUTHOR_NAME": "github-actions[bot]",
        "GIT_AUTHOR_EMAIL": "41898282+github-actions[bot]@users.noreply.github.com",
        "GIT_COMMITTER_NAME": "github-actions[bot]",
        "GIT_COMMITTER_EMAIL": "41898282+github-actions[bot]@users.noreply.github.com",
    })
    result = subprocess.run(["git", *args], cwd=ROOT, env=environment,
                            input=data, text=True, capture_output=True, check=True)
    return result.stdout.strip()


def run():
    repository = os.environ["GITHUB_REPOSITORY"]
    run_id = os.environ["GITHUB_RUN_ID"]
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    report = {
        "head_sha": HEAD,
        "run_id": run_id,
        "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT", "1"),
        "stage": STAGE,
        "status": os.environ["PAIMBNAILS_CI_STATUS"],
        "run_url": f"{server}/{repository}/actions/runs/{run_id}",
        "updated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    }
    diagnostic = ROOT / "build-diagnostics.log"
    if diagnostic.is_file():
        report["diagnostics"] = diagnostic.read_text(encoding="utf-8", errors="replace")[-80000:]
    if STAGE == "summary":
        jobs = json.loads(api(f"/repos/{repository}/actions/runs/{run_id}/jobs?per_page=100"))["jobs"]
        report["jobs"] = []
        for job in jobs:
            if job["name"] == "Publish build results":
                continue
            entry = {key: job[key] for key in ("id", "name", "status", "conclusion", "html_url")}
            if job["conclusion"] == "failure":
                try:
                    entry["log"] = api(f"/repos/{repository}/actions/jobs/{job['id']}/logs").decode(
                        "utf-8", errors="replace")[-160000:]
                except urllib.error.HTTPError as error:
                    entry["log_error"] = f"HTTP {error.code}"
            report["jobs"].append(entry)
        conclusions = {job["conclusion"] for job in report["jobs"]}
        if conclusions & {"failure", "timed_out", "action_required"}:
            report["status"] = "failure"
        elif "cancelled" in conclusions:
            report["status"] = "cancelled"
        elif any(job["status"] != "completed" for job in report["jobs"]):
            report["status"] = "in_progress"
        else:
            report["status"] = "success"

    serialized = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if TOKEN:
        serialized = serialized.replace(TOKEN, "***")
    blob = git("hash-object", "-w", "--stdin", data=serialized)
    tree = git("mktree", data=f"100644 blob {blob}\treport.json\n")
    ref = f"refs/heads/ci-results/{HEAD}/{STAGE}"
    parents = []
    if git("ls-remote", "origin", ref):
        git("fetch", "--depth=1", "origin", ref)
        parents = ["-p", git("rev-parse", "FETCH_HEAD")]
    commit = git("commit-tree", tree, *parents,
                 "-m", f"CI {STAGE}: {report['status']} [skip ci]")
    git("push", "origin", f"{commit}:{ref}")
    print(f"Published {STAGE}: {report['status']} ({report['run_url']})")


if __name__ == "__main__":
    run()
