#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
REPO="CrownOpsEng/clipreg"
APPLY=0
PROTECT=1

usage(){ cat <<'USAGE'
Usage: scripts/configure-github.sh [--repo OWNER/REPO] [--apply] [--no-protect]

Dry-run by default. With --apply, reconcile public repository settings, labels,
v0.2.0 milestone, and (unless --no-protect) main branch protection. Protection
requires a successful Check run on current main.
USAGE
}
while (($#)); do
  case "$1" in
    --repo) shift; REPO="${1:?--repo requires OWNER/REPO}" ;;
    --apply) APPLY=1 ;;
    --no-protect) PROTECT=0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
command -v gh >/dev/null || { echo 'ERROR: gh required' >&2; exit 1; }
gh auth status >/dev/null
VISIBILITY="$(gh repo view "$REPO" --json visibility --jq .visibility)"
[[ "$VISIBILITY" == PUBLIC ]] || { echo "ERROR: $REPO must be public before applying public-repo protection" >&2; exit 1; }

cat <<PLAN
Repository: $REPO
Visibility: public
Merge policy: squash-only; auto-merge/update-branch enabled; merged branches deleted
Issues: enabled; wiki/projects disabled
Labels: priority:blocker, area:runtime, area:deployment, area:github, release:0.2
Milestone: v0.2.0
main: PR required, Check required/up-to-date, conversations resolved, 0 approvals,
      admins enforced, linear history, no force-push/delete
PLAN
((APPLY)) || { echo 'PLAN ONLY — rerun with --apply to mutate GitHub.'; exit 0; }

gh api --method PATCH "repos/$REPO" \
  -F has_issues=true -F has_wiki=false -F has_projects=false \
  -F allow_squash_merge=true -F allow_merge_commit=false -F allow_rebase_merge=false \
  -F allow_auto_merge=true -F delete_branch_on_merge=true -F allow_update_branch=true >/dev/null

gh api --method PUT "repos/$REPO/topics" --input - >/dev/null <<'JSON'
{"names":["wayland","cosmic","clipboard","linux","pop-os","productivity","c"]}
JSON

for spec in \
  'bug|D73A4A|Something is not working' \
  'enhancement|A2EEEF|New feature or request' \
  'priority:blocker|B60205|Blocks the next release promotion' \
  'area:runtime|1D76DB|Native daemon, protocol, transaction, or persistence behavior' \
  'area:deployment|0E8A16|Install, upgrade, service, udev, migration, or packaging' \
  'area:github|5319E7|Repository, CI, issue, PR, or release tooling' \
  'release:0.2|FBCA04|Targets the v0.2.0 line'; do
  IFS='|' read -r name color desc <<<"$spec"
  gh label create "$name" --repo "$REPO" --color "$color" --description "$desc" --force >/dev/null
done

if ! gh api "repos/$REPO/milestones?state=all&per_page=100" --jq '.[].title' | grep -Fxq 'v0.2.0'; then
  gh api --method POST "repos/$REPO/milestones" -f title='v0.2.0' -f description='Native transactional ClipReg v0.2 stabilization and qualification.' >/dev/null
fi

if ((PROTECT)); then
  MAIN_SHA="$(gh api "repos/$REPO/commits/main" --jq .sha)"
  CHECK_OK="$(gh api "repos/$REPO/commits/$MAIN_SHA/check-runs" --jq '[.check_runs[] | select(.name=="Check" and .conclusion=="success")] | length')"
  [[ "$CHECK_OK" -ge 1 ]] || { echo 'ERROR: current main has no successful Check run; refusing to protect against an unproven context' >&2; exit 1; }
  gh api --method PUT "repos/$REPO/branches/main/protection" --input - >/dev/null <<'JSON'
{
  "required_status_checks":{"strict":true,"contexts":["Check"]},
  "enforce_admins":true,
  "required_pull_request_reviews":{
    "dismiss_stale_reviews":false,
    "require_code_owner_reviews":false,
    "required_approving_review_count":0,
    "require_last_push_approval":false
  },
  "restrictions":null,
  "required_linear_history":true,
  "allow_force_pushes":false,
  "allow_deletions":false,
  "block_creations":false,
  "required_conversation_resolution":true,
  "lock_branch":false,
  "allow_fork_syncing":true
}
JSON
fi

echo "GitHub configuration reconciled: https://github.com/$REPO"
