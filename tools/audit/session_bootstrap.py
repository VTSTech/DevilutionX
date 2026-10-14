#!/usr/bin/env python3
"""session_bootstrap.py - one-shot state snapshot + drift check for resuming work.

Replaces the "re-read the whole handoff doc and re-verify the repo by hand"
ritual at the start of every session. Prints ~a dozen lines:

  - repo identity: branch, tip, dirty files, origin sync (as of last fetch)
  - release tags (tip-most gpu-shaders-* tag)
  - handoff-drift: does docs/diablo-handoff.md's section-2 tip cell match the
    actual tip? Commits since the doc round are tagged docs-only (benign) or
    code (re-verify). Dead hashes mentioned in the doc are listed separately
    (rebase casualties - content anchors only).
  - field ledger status: working / fallback / pending rows in the tiered
    shader-test-list CSV
  - field-round commits (git log --grep 'Field-Round:')
  - worklog tail (last Task ID + title), if the sandbox worklog exists

Usage:  python session_bootstrap.py [--repo PATH] [--worklog PATH]
Exit codes: 0 ok, 1 not a git repo / git missing. Stdlib only.
"""
import argparse
import csv
import datetime
import os
import re
import subprocess
import sys

try:
    from zoneinfo import ZoneInfo

    def today():
        return datetime.datetime.now(ZoneInfo("America/Toronto")).date().isoformat()
except Exception:  # pragma: no cover
    def today():
        return datetime.datetime.now(datetime.timezone.utc).date().isoformat()

RE_DEFAULT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RE_ROW = re.compile(r"^\|\s*fork `master`[^|]*\|\s*`?([0-9a-f]{9,40})`?", re.M)
RE_HEX = re.compile(r"\b[0-9a-f]{9,40}\b")


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args),
                          capture_output=True, text=True, check=True).stdout.strip()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", default=RE_DEFAULT)
    ap.add_argument("--worklog", default="/home/z/my-project/worklog.md")
    ap.add_argument("--doc", default="docs/diablo-handoff.md")
    args = ap.parse_args(argv)
    repo = os.path.abspath(args.repo)

    try:
        git(repo, "rev-parse", "HEAD")
    except Exception:
        print("session_bootstrap: %s is not a git repo (or git missing)" % repo,
              file=sys.stderr)
        return 1

    out = ["=== session bootstrap | %s ===" % today()]
    branch = git(repo, "rev-parse", "--abbrev-ref", "HEAD")
    tip = git(repo, "log", "-1", "--format=%h %s")
    dirty = len(git(repo, "status", "--porcelain").splitlines())
    sync = ""
    try:
        ahead = int(git(repo, "rev-list", "--count", "origin/master..HEAD"))
        behind = int(git(repo, "rev-list", "--count", "HEAD..origin/master"))
        sync = "origin/master: ahead %d, behind %d (as of last fetch)" % (ahead, behind)
    except Exception:
        sync = "origin/master: unknown (no fetch yet)"
    out.append("repo: %s | branch: %s | dirty: %d | %s" % (repo, branch, dirty, sync))
    out.append("tip: %s" % tip)

    tags = git(repo, "for-each-ref", "refs/tags/gpu-shaders-*",
               "--format=%(refname:short) %(objectname:short)",
               "--sort=-creatordate").splitlines()
    if tags:
        out.append("release tags: %s (tip-most; %d total)" % (tags[0], len(tags)))

    # ---- handoff drift ----------------------------------------------------
    doc_path = os.path.join(repo, args.doc)
    if os.path.isfile(doc_path):
        with open(doc_path, "r", encoding="utf-8", errors="replace") as fh:
            doc = fh.read()
        m = RE_ROW.search(doc)
        if m:
            doctip = m.group(1)
            try:
                full = git(repo, "rev-parse", doctip)
            except Exception:
                full = None
            if full == git(repo, "rev-parse", "HEAD"):
                out.append("handoff drift: none (%s == tip)" % doctip[:9])
            elif full is None:
                out.append("handoff drift: section-2 tip %s does not resolve - "
                           "stale hash, distrust the doc tip" % doctip[:9])
            else:
                anc = subprocess.run(
                    ["git", "-C", repo, "merge-base", "--is-ancestor", doctip, "HEAD"],
                    capture_output=True)
                if anc.returncode != 0:
                    out.append("handoff drift: section-2 tip %s is not an ancestor "
                               "of HEAD (dangling pre-rebase object?) - treat as stale"
                               % doctip[:9])
                else:
                    n = int(git(repo, "rev-list", "--count",
                                "%s..HEAD" % doctip) or 0)
                    files = git(repo, "diff", "--name-only",
                                "%s..HEAD" % doctip).splitlines()
                    docs_only = bool(files) and all(
                        f.startswith(("docs/", "tools/audit/")) for f in files)
                    verdict = ("benign - docs/tooling only; refresh section 2 on next "
                               "doc round" if docs_only else
                               "CODE changed since the doc round - re-verify before "
                               "trusting")
                    out.append("handoff drift: section-2 tip %s -> %d commits since (%s)"
                               % (doctip[:9], n, verdict))
        dead = []
        # caveat: checks the LOCAL object DB - pre-rebase objects can linger as
        # dangling until gc, so "resolves here" != "resolves on a fresh clone".
        for h in sorted(set(RE_HEX.findall(doc))):
            r = subprocess.run(["git", "-C", repo, "cat-file", "-e",
                                "%s^{commit}" % h], capture_output=True)
            if r.returncode != 0:
                dead.append(h)
        if dead:
            out.append("handoff dead hashes (rebased? content anchors only): %s"
                       % " ".join(dead))

    # ---- field ledger -----------------------------------------------------
    ledger = os.path.join(repo, "docs/audit")
    if os.path.isdir(ledger):
        import glob
        for path in sorted(glob.glob(os.path.join(ledger, "shader-test-list-*.csv"))):
            counts = {}
            total = 0
            with open(path, newline="", encoding="utf-8", errors="replace") as fh:
                for row in csv.DictReader(fh):
                    total += 1
                    st = (row.get("status") or "").strip().lower()
                    counts[st or "pending(blank)"] = counts.get(st or "pending(blank)", 0) + 1
            pretty = " | ".join("%s %d" % (k, v)
                                for k, v in sorted(counts.items(), key=lambda x: -x[1]))
            out.append("ledger %s: %d presets | %s"
                       % (os.path.basename(path), total, pretty))

    # ---- field-round commits ----------------------------------------------
    fr = git(repo, "log", "--grep=Field-Round", "--format=%h %s", "-5").splitlines()
    if fr:
        out.append("field-round commits: %s" % "; ".join(fr))

    # ---- worklog tail ------------------------------------------------------
    if os.path.isfile(args.worklog):
        with open(args.worklog, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.read().splitlines()
        tid = task = None
        for i, ln in enumerate(lines):
            m = re.match(r"Task ID: (\S+)", ln)
            if m:
                tid = m.group(1)
                task = next((lines[j].strip() for j in range(i + 1, min(i + 4, len(lines)))
                             if lines[j].startswith("Task:")), task)
        if tid:
            out.append("worklog: last Task ID %s - %s" % (tid, (task or "")[:110]))

    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
