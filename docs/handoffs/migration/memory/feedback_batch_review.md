---
name: feedback-batch-review
description: Do not interrupt minimum implementation sub-steps for per-step code review; batch review with team agents + skills after the milestone scope is complete
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d2528455-b731-41b5-837f-2a33339de72d
---

When working through a milestone's minimum implementation sub-steps (e.g. M1 walking skeleton → PerfTracker → RingLogger → crash handler → working set hook), do NOT pause to spawn review agents between sub-steps.

**Why:** User explicitly requested on 2026-05-14: "모든 최소 구현후 별도 팀 에이전트 활용 및 스킬로 리뷰". User wants the implementation to flow continuously and prefers a single consolidated review at the end of the milestone rather than fragmented per-step reviews.

**How to apply:**
- Within a milestone, keep executing sub-steps back-to-back. Self-check each sub-step for build + smoke test, but skip spawning `rkit:code-analyzer`, `rkit:c-cpp-reviewer`, `pr-review-toolkit:*`, or `cavecrew-reviewer` agents between sub-steps.
- After **all** identified minimum sub-steps for the milestone are committed, run the consolidated review:
  - Spawn team-mode review agents (L1 `rkit:code-analyzer`, L2 `rkit:c-cpp-reviewer`, optionally `pr-review-toolkit:code-reviewer`) in parallel against the milestone's accumulated diff.
  - Also invoke relevant skills (e.g. `rkit:code-review`, `caveman:caveman-review`, `simplify`).
- If a user choice question pops up mid-milestone via `AskUserQuestion`, do not offer "review now" as an option until all minimum sub-steps are done — only offer it as the closing step.
- Critical/blocking issues spotted while coding (compile errors, obvious UB) should still be fixed immediately; this rule is about not soliciting external review opinions between sub-steps.
