---
name: commit
description: Build-verify and commit changes with submodule-aware workflow. Use when the user asks to commit, or after completing a feature/fix.
---

# Commit Skill

## Pre-flight

1. **Build gate** — A `PreToolUse` hook in `.claude/settings.local.json` automatically runs `make all`, `clang-format`, and `make smoke` before any `git commit`. The smoke test launches the engine for ~20 frames to catch startup crashes. Do NOT run these manually — the hook handles it. If a hook reports a failure, fix the issue before retrying.
2. **Submodule check** — Run `git submodule status`. The only submodule is `lib/rmlui`. If prefixed with `+`, the pointer has moved.
   - If the user's work involved RmlUI submodule changes, those need to be committed **inside the submodule first**, then the updated pointer committed in the main repo.
   - If the submodule pointer moved but the user didn't intentionally change it, ask before staging it — it may be an accidental pointer drift.
   - Use `git diff --submodule=log` to show what commits the pointer moved across.

## Staging

3. **Review all changes** — Run `git status` and `git diff` (unstaged) and `git diff --staged` (staged).
   - Show the user a clear summary of what's staged vs unstaged.
   - Flag any files that look like they don't belong (`.cfg` written by the engine, accidental binary files, etc.).
   - Never stage `game/` contents (it's gitignored runtime data).

## Commit message

4. **Match existing style** — Check `git log --oneline -5` for recent message patterns. This repo uses:
   - `Area: short imperative description` format (e.g. `RmlUI: add Modern HUD toggle`)
   - Common prefixes: `RmlUI`, `Post-process`, `Sprint FOV`, `Engine`, `Build`, `UI`
   - Keep subject under 72 characters
   - Add body paragraph only if the change needs explanation beyond the subject line

## Confirmation

5. **Show summary and ask** — Present:
   - Files to be committed (with submodule pointer changes called out separately)
   - Proposed commit message
   - Ask user to confirm, edit the message, or make additional changes before committing

6. **Commit** — Only after user confirms. Use `Co-Authored-By` trailer as required.

## Submodule workflow reference

The only submodule is `lib/rmlui`. When committing RmlUI submodule changes:
```
# 1. Inside the submodule — commit and push
cd lib/rmlui/
git add <files>
git commit -m "Description of submodule change"
git push origin rmlui

# 2. Back in main repo — stage the updated pointer
cd /path/to/project-root
git add lib/rmlui
git commit -m "Update RmlUI submodule (description of why)"
```

Submodule pointer commits should explain *what changed* in the submodule, e.g.:
- `Update RmlUI submodule (add custom element support)`
