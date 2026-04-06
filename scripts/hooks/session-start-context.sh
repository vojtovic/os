#!/usr/bin/env sh

# Non-blocking guidance hook: inject shared collaboration context location.
printf '%s\n' '{"continue": true, "systemMessage": "Shared multi-agent context is in .github/agent-handoff.md. Read it before planning, and update it after major decisions."}'
