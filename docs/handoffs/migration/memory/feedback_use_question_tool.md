---
name: feedback-use-question-tool
description: Always use AskUserQuestion tool for clarifying or branching questions instead of plain-text question prompts at the end of responses
metadata: 
  node_type: memory
  type: feedback
  originSessionId: d2528455-b731-41b5-837f-2a33339de72d
---

When asking the user to choose between options or clarify intent, ALWAYS use the `AskUserQuestion` tool rather than ending a response with a plain-text question.

**Why:** User explicitly requested this on 2026-05-14 mid-way through the fast-explorer project ("앞으로 질문은 툴을 사용해줘"). User preference for structured option-based input over free-form prose questions.

**How to apply:**
- Any time the model would otherwise end with a "X 진행할까?", "A 또는 B?", "어느 쪽으로 갈까?" style question — wrap it in `AskUserQuestion` with 2–4 concrete options.
- Skip the tool only when the question is purely informational follow-up (no branching decision) or the user has already given clear directives within the same turn.
- Caveman-mode tone is fine inside option labels/descriptions; just keep them short.
