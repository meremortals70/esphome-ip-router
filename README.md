# AI Autonomy Steward

A Home Assistant App that continuously reasons about your home's current
state, proposes actions for one-time approval, then acts autonomously -
bounded by an append-only constraint layer that only ever grows stricter.

Part of the Abode AI Autonomy system. In its four-layer architecture:
System 0 is perception (Home Assistant's sensors, and later Frigate/Tara),
System 1 is fast deterministic automations, **System 2 is this App**, and a
separate, independent constraint layer gates everything it proposes before
execution.

## Two components, install both

- **`ai_autonomy_steward`** - the App itself (Supervisor-managed container,
  independent of HA Core's own restart cycle).
- **`custom_components/ai_autonomy_steward_config`** - a companion
  integration that runs inside HA Core, purely to provide real
  entity/service selectors for `notify_service` and the AI Task entity.
  The App's own options schema has no entity type, so this is the only way
  those two fields can be validated against your actual HA data instead of
  typed as free text.

## What it does

It subscribes to Home Assistant's event stream and actively waits - it is
never on a timer, never polling. When anything changes, it wakes, pulls
the **complete current state fresh** (not a diff, the whole picture, every
time), and reasons about whether anything is worth doing. A burst of
several changes at once coalesces into a single reasoning pass rather than
firing one per event, but the wake condition is always "something
changed," never "time has passed."

- **New kind of action:** proposed to you via mobile notification
  (Approve / Deny / Never). It does not act until approved.
- **Already-approved kind of action:** still decides fresh, every pass,
  whether this specific moment actually calls for it.
- **A better way to do something already approved:** it can propose
  retiring or replacing an approved action, subject to the same approval
  step.

## The constraint layer

Independent of the reasoning above. Every proposed action - including
already-approved ones - is checked against `constraints.yaml` before it's
allowed to execute. Tapping **Never** on any proposal permanently appends
a new rule. Nothing in the running system can remove a constraint once
added; the file only ever grows stricter. Constraints are only ever
removed by editing `constraints.yaml` directly, outside the running app.

## Installation

1. Install the companion integration: copy
   `custom_components/ai_autonomy_steward_config` into your HA `config/
   custom_components/` folder (or via HACS custom repository), restart HA,
   then Settings → Devices & Services → Add Integration → search
   "AI Autonomy Steward Config" → select your real `notify.*` service and
   your real `ai_task.*` entity.
2. Install the App: Settings → Add-ons → Add-on Store → ⋮ → Repositories
   → add this repo's URL → find **AI Autonomy Steward** → Install.
3. Configuration tab → confirm `min_confidence` (default 0.70) - this is
   the only App-level option now.
4. Start the app, check the Log tab for "Authenticated against Home
   Assistant Core" and "AI Autonomy Steward started, actively waiting for
   changes."

## Configuration

| Field | Where it's set | Description |
|---|---|---|
| `notify_service` | Companion integration (entity selector) | Real `notify.*` service for approval prompts |
| `ai_task_entity` | Companion integration (entity selector) | Real `ai_task.*` entity used for reasoning passes |
| `min_confidence` | App options | Won't act or propose below this confidence (0-1) |

## Status

Second build. First build wrongly used a fixed polling interval; corrected
to genuine event-driven wake per the settled architecture. Unexercised in
production either way. Frigate and Tara are not yet wired in as input
sources - the code is structured so they can be added later without
redesigning the reasoning loop.

## License

MIT
