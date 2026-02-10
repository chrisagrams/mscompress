#!/usr/bin/env bash
# Fetch GitHub Copilot PR review comments using `gh` CLI and generate a markdown report.
#
# Usage: ./fetch_copilot_comments.sh <owner/repo> <pr_number> [output_file]
#
# Requires: gh (authenticated), jq

set -euo pipefail

if [ $# -lt 2 ]; then
  echo "Usage: $0 <owner/repo> <pr_number> [output_file]" >&2
  exit 1
fi

REPO="$1"
PR="$2"
OUTPUT="${3:-copilot_review.md}"

# Validate tools
for cmd in gh jq; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "Error: '$cmd' is required but not installed." >&2
    exit 1
  fi
done

echo "Fetching PR #${PR} from ${REPO}..."

# Fetch PR metadata
PR_JSON=$(gh pr view "$PR" --repo "$REPO" --json title,author,url,createdAt)
PR_TITLE=$(echo "$PR_JSON" | jq -r '.title')
PR_AUTHOR=$(echo "$PR_JSON" | jq -r '.author.login')
PR_URL=$(echo "$PR_JSON" | jq -r '.url')
PR_CREATED=$(echo "$PR_JSON" | jq -r '.createdAt')

# Fetch all review comments (inline code comments)
echo "Fetching review comments..."
REVIEW_COMMENTS=$(gh api --paginate "repos/${REPO}/pulls/${PR}/comments" 2>/dev/null || echo "[]")

# Fetch top-level reviews
echo "Fetching reviews..."
REVIEWS=$(gh api --paginate "repos/${REPO}/pulls/${PR}/reviews" 2>/dev/null || echo "[]")

# Fetch issue-level comments
echo "Fetching issue comments..."
ISSUE_COMMENTS=$(gh api --paginate "repos/${REPO}/issues/${PR}/comments" 2>/dev/null || echo "[]")

# Filter to copilot authors
COPILOT_FILTER='[.[] | select(
  (.user.login | ascii_downcase) == "copilot" or
  (.user.login | ascii_downcase) == "copilot[bot]" or
  (.user.login | ascii_downcase) == "github-copilot" or
  (.user.login | ascii_downcase) == "github-copilot[bot]" or
  (.user.login | ascii_downcase) == "github-advanced-security[bot]" or
  ((.user.type | ascii_downcase) == "bot" and ((.user.login | ascii_downcase) | contains("copilot")))
)]'

COPILOT_INLINE=$(echo "$REVIEW_COMMENTS" | jq "$COPILOT_FILTER")
COPILOT_REVIEWS=$(echo "$REVIEWS" | jq "[.[] | select(
  ((.user.login | ascii_downcase) == \"copilot\" or
   (.user.login | ascii_downcase) == \"copilot[bot]\" or
   (.user.login | ascii_downcase) == \"github-copilot\" or
   (.user.login | ascii_downcase) == \"github-copilot[bot]\" or
   (.user.login | ascii_downcase) == \"github-advanced-security[bot]\" or
   ((.user.type | ascii_downcase) == \"bot\" and ((.user.login | ascii_downcase) | contains(\"copilot\"))))
  and (.body != null) and (.body != \"\")
)]")
COPILOT_ISSUE=$(echo "$ISSUE_COMMENTS" | jq "$COPILOT_FILTER")

# Counts
INLINE_COUNT=$(echo "$COPILOT_INLINE" | jq 'length')
REVIEW_COUNT=$(echo "$COPILOT_REVIEWS" | jq 'length')
ISSUE_COUNT=$(echo "$COPILOT_ISSUE" | jq 'length')
TOTAL=$((INLINE_COUNT + REVIEW_COUNT + ISSUE_COUNT))

# Get list of unique files
FILES_JSON=$(echo "$COPILOT_INLINE" | jq '[.[].path] | unique | sort')
FILE_COUNT=$(echo "$FILES_JSON" | jq 'length')

# Count comments with suggestion blocks
SUGGESTION_COUNT=$(echo "$COPILOT_INLINE" | jq '[.[] | select(.body | contains("```suggestion"))] | length')

echo "Generating markdown report..."

{
  # Header
  cat <<EOF
# 🤖 GitHub Copilot Review: ${PR_TITLE}

**PR:** [${REPO}#${PR}](${PR_URL})
**Author:** @${PR_AUTHOR}
**Created:** ${PR_CREATED}

---

## 📊 Summary

- **Total Copilot comments:** ${TOTAL}
  - Inline code comments: ${INLINE_COUNT}
  - Review summaries: ${REVIEW_COUNT}
  - General comments: ${ISSUE_COUNT}
  - Comments with code suggestions: ${SUGGESTION_COUNT}
- **Files reviewed:** ${FILE_COUNT}
EOF

  # List files
  echo "$FILES_JSON" | jq -r '.[] | "  - `\(.)`"'

  echo ""
  echo "---"
  echo ""

  # Review summaries
  if [ "$REVIEW_COUNT" -gt 0 ]; then
    echo "## 📝 Review Summaries"
    echo ""
    echo "$COPILOT_REVIEWS" | jq -r '.[] |
      "**Review state:** \(.state | gsub("_"; " ") | ascii_upcase)\n" +
      "🔗 [View on GitHub](\(.html_url))\n\n" +
      "\(.body)\n\n---\n"'
  fi

  # Inline comments grouped by file
  if [ "$INLINE_COUNT" -gt 0 ]; then
    echo "## 💬 Inline Code Comments"
    echo ""

    # Iterate over each unique file
    echo "$FILES_JSON" | jq -r '.[]' | while IFS= read -r filepath; do
      echo "### 📁 \`${filepath}\`"
      echo ""

      # Get comments for this file, sorted by line number
      echo "$COPILOT_INLINE" | jq -r --arg path "$filepath" '
        [.[] | select(.path == $path)] | sort_by(.original_line // .line // .position // 0) | .[] |
        "#### Line \(.original_line // .line // .position // "N/A")\n" +
        "🔗 [View on GitHub](\(.html_url))\n" +
        "🕐 \(.created_at)\n" +
        (if .diff_hunk then
          "\n<details><summary>Diff context</summary>\n\n```diff\n\(.diff_hunk)\n```\n</details>\n"
        else "" end) +
        "\n\(.body)\n\n---\n"'
    done
  fi

  # General issue comments
  if [ "$ISSUE_COUNT" -gt 0 ]; then
    echo "## 🗨️ General Comments"
    echo ""
    echo "$COPILOT_ISSUE" | jq -r '.[] |
      "🔗 [View on GitHub](\(.html_url))\n🕐 \(.created_at)\n\n\(.body)\n\n---\n"'
  fi

  # No comments found
  if [ "$TOTAL" -eq 0 ]; then
    cat <<EOF
## ℹ️ No Copilot Comments Found

No comments from GitHub Copilot were found on this PR.

This could mean:
- Copilot code review was not enabled for this repository
- Copilot did not flag any issues
- The PR has not been reviewed by Copilot yet
EOF
  fi

} > "$OUTPUT"

echo "✅ Report saved to ${OUTPUT} (${TOTAL} Copilot comments found)"