# Team Brief: audio_rewire

Date: 2026-08-17

## Goal
Redesign independent I2S wiring for Sesame V3 ESP32-S3 using only exposed pins, avoiding shared BCLK and WS.

## Topology
- Choice: lead-hub
- Note: Lead assigns and integrates independent specialists.

## Team Modes
- Communication: direct
- Delegate mode: enabled

## Definition of Done
- All workstreams have a named owner and status.
- Verification commands pass for changed scope.
- Open risks and follow-ups are recorded.

## Constraints
- No additional constraints supplied.

## Skill Links
- superpowers:brainstorming
- superpowers:writing-plans
- superpowers:subagent-driven-development
- superpowers:dispatching-parallel-agents
- superpowers:verification-before-completion

## Roles
| Role | Mission | Deliverables |
| --- | --- | --- |
| lead | Coordinate priorities, unblock teammates, and integrate final output. | Plan, decision log, final integration summary |
| hardware specialist | Define mission for this role. | Define expected deliverables. |
| audio firmware specialist | Define mission for this role. | Define expected deliverables. |

## Workstreams
| Workstream | Owner | Status | Verification |
| --- | --- | --- | --- |
| hardware-pin-audit | TBD | planned | TBD |
| i2s-software-audit | TBD | planned | TBD |

## Handoff Protocol
1. Lead posts assignment with owner, boundary, and deadline.
2. Specialist returns summary, touched scope, verification output, and risks.
3. Reviewer validates spec and quality before status changes to complete.
4. Lead records decision and next action in this brief.

## Debate and Reflection Protocol
1. For conflicting approaches, open a formal debate linked to the blocked task.
2. Require one position per debate member with option, rationale, and confidence.
3. Decide with explicit rationale, then apply outcome to task status/owner.
4. Broadcast the chosen path and resume implementation on the linked task.

## Decision Log
- [ ] Decision:
  Context:
  Chosen option:
  Why:
  Follow-up:
