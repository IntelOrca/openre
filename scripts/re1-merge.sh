#!/usr/bin/env sh
# Merges the RE1 port work branch (re1-port) into the source branch, stripping
# branch-local notes so only progress code lands in the source branch.
#
# Usage:
#   scripts/re1-merge.sh [source-branch]
#
# The source branch defaults to `ecs`. It may become `master` in future; pass
# it explicitly when that happens.
#
# Stripped on merge:
#   - docs/re1-port/  (all port notes)
#   - AGENTS.md       (reverted to the source branch's version; the RE1
#                      workflow section is branch-local too)
set -e

WORK_BRANCH="re1-port"
SOURCE_BRANCH="${1:-ecs}"

# Only ever run from the work branch.
CURRENT_BRANCH="$(git symbolic-ref --short HEAD 2>/dev/null || true)"
if [ "$CURRENT_BRANCH" != "$WORK_BRANCH" ]; then
    echo "error: must run from the '$WORK_BRANCH' branch (currently on '$CURRENT_BRANCH')" >&2
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "error: working tree is dirty; commit or stash before merging" >&2
    exit 1
fi

git checkout "$SOURCE_BRANCH"

if ! git merge --no-commit --no-ff "$WORK_BRANCH"; then
    echo "error: merge conflicts; resolve them, commit manually, then return to $WORK_BRANCH" >&2
    exit 1
fi

# Strip branch-local notes.
git rm -r -q --ignore-unmatch docs/re1-port

# Keep the source branch's AGENTS.md (revert any work-branch changes to it).
git checkout HEAD -- AGENTS.md

git commit --no-edit

echo "merged $WORK_BRANCH into $SOURCE_BRANCH (notes stripped)"
git checkout "$WORK_BRANCH"