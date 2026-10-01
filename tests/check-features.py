#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate project_doc/features.yaml, the feature status list the website reads.

Requires PyYAML (`apt install python3-yaml`, or `uv run --with pyyaml`).
"""

import datetime
from pathlib import Path
import re
import sys

import yaml

ROOT = Path(__file__).resolve().parent.parent
FEATURES = ROOT / "project_doc" / "features.yaml"
ROADMAP = ROOT / "project_doc" / "roadmap.md"

SCHEMA = 1
STATUSES = {"wish", "planned", "in_progress", "completed"}
PLATFORMS = {"rpi3", "rpi4", "qemu-raspi3b", "qemu-virt"}
ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

TOP_KEYS = {"schema", "repository", "categories"}
CATEGORY_KEYS = {"id", "name", "description", "features"}
FEATURE_REQUIRED = {"id", "name", "status"}
FEATURE_KEYS = FEATURE_REQUIRED | {
    "description", "milestone", "platforms", "prs", "completed", "version"}


def roadmap_milestones():
    text = ROADMAP.read_text(encoding="utf-8")
    return {int(m) for m in re.findall(r"^## Milestone (\d+)\b", text, re.M)}


def is_date(value):
    if isinstance(value, datetime.date):
        return True
    if isinstance(value, str) and DATE_RE.match(value):
        try:
            datetime.date.fromisoformat(value)
            return True
        except ValueError:
            return False
    return False


def check_keys(where, item, allowed, required, errors):
    if not isinstance(item, dict):
        errors.append(f"{where}: expected a mapping")
        return False
    for key in sorted(set(item) - allowed):
        errors.append(f"{where}: unknown key '{key}'")
    for key in sorted(required - set(item)):
        errors.append(f"{where}: missing '{key}'")
    return True


def check_feature(where, f, milestones, errors):
    status = f.get("status")
    if status not in STATUSES:
        errors.append(f"{where}: status must be one of {sorted(STATUSES)}")
    for key in ("name", "description"):
        if key in f and not (isinstance(f[key], str) and f[key].strip()):
            errors.append(f"{where}: '{key}' must be a non-empty string")
    if "milestone" in f and f["milestone"] not in milestones:
        errors.append(f"{where}: milestone {f['milestone']!r} is not in roadmap.md")
    if "platforms" in f:
        p = f["platforms"]
        if not (isinstance(p, list) and p and set(p) <= PLATFORMS
                and len(p) == len(set(p))):
            errors.append(f"{where}: platforms must be a non-empty, duplicate-free "
                          f"subset of {sorted(PLATFORMS)}")
    if "prs" in f:
        prs = f["prs"]
        if not (isinstance(prs, list) and prs and all(
                type(n) is int and n > 0 for n in prs) and len(prs) == len(set(prs))):
            errors.append(f"{where}: prs must be a non-empty list of distinct PR numbers")
    if status == "completed":
        if not is_date(f.get("completed")):
            errors.append(f"{where}: completed features need 'completed: YYYY-MM-DD'")
    else:
        for key in ("completed", "version"):
            if key in f:
                errors.append(f"{where}: '{key}' is only allowed on completed features")
    if "version" in f and not (isinstance(f["version"], str) and f["version"].strip()):
        errors.append(f"{where}: version must be a non-empty string")


def validate(doc, milestones):
    errors = []
    if not check_keys("top level", doc, TOP_KEYS, TOP_KEYS, errors):
        return errors
    if doc.get("schema") != SCHEMA:
        errors.append(f"top level: schema must be {SCHEMA}")
    if not str(doc.get("repository", "")).startswith("https://"):
        errors.append("top level: repository must be an https URL")
    categories = doc.get("categories")
    if not isinstance(categories, list) or not categories:
        errors.append("top level: categories must be a non-empty list")
        return errors

    category_ids, feature_ids = set(), set()
    for ci, cat in enumerate(categories):
        where = f"categories[{ci}]"
        if not check_keys(where, cat, CATEGORY_KEYS, {"id", "name", "features"}, errors):
            continue
        cid = cat.get("id")
        if not (isinstance(cid, str) and ID_RE.match(cid)):
            errors.append(f"{where}: id must be lowercase kebab-case")
        elif cid in category_ids:
            errors.append(f"{where}: duplicate category id '{cid}'")
        category_ids.add(cid)
        where = f"category '{cid}'"
        features = cat.get("features")
        if not isinstance(features, list) or not features:
            errors.append(f"{where}: features must be a non-empty list")
            continue
        for fi, f in enumerate(features):
            fwhere = f"{where} features[{fi}]"
            if not check_keys(fwhere, f, FEATURE_KEYS, FEATURE_REQUIRED, errors):
                continue
            fid = f.get("id")
            if not (isinstance(fid, str) and ID_RE.match(fid)):
                errors.append(f"{fwhere}: id must be lowercase kebab-case")
            elif fid in feature_ids:
                errors.append(f"{fwhere}: duplicate feature id '{fid}'")
            else:
                fwhere = f"feature '{fid}'"
            feature_ids.add(fid)
            check_feature(fwhere, f, milestones, errors)
    return errors


def main():
    try:
        doc = yaml.safe_load(FEATURES.read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        print(f"{FEATURES.relative_to(ROOT)}: invalid YAML: {exc}", file=sys.stderr)
        return 1
    errors = validate(doc, roadmap_milestones())
    for error in errors:
        print(f"{FEATURES.relative_to(ROOT)}: {error}", file=sys.stderr)
    if errors:
        return 1
    count = sum(len(c["features"]) for c in doc["categories"])
    print(f"{FEATURES.relative_to(ROOT)}: {count} features in "
          f"{len(doc['categories'])} categories OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
