#!/usr/bin/env sh

# Non-blocking reminder hook after successful tool usage.
printf '%s\n' '{"continue": true, "systemMessage": "If you changed behavior, decisions, or next steps, update .github/agent-handoff.md so other agents can continue reliably."}'
