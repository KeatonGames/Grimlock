# Player progression

Grimlock Game keeps a single local progression file, separate from levels and
scenes. Editor never opens or writes it. There are no mid-battle saves, locked
levels or mandatory campaign ordering.

## Save location

Game uses `SDL_GetPrefPath("KeatonGames", "Grimlock")` and stores `progress.json`
there. On Windows this is normally:

```text
%APPDATA%\KeatonGames\Grimlock\progress.json
```

The path is independent of the executable's working directory and `--assets`.
Other platforms use SDL's corresponding per-user application-data location.
Tests inject temporary paths under the build tree, never the real player save.
The first successfully loaded, identified level creates the save.

Version-1 JSON contains `last_played` (a level ID) and a `levels` object keyed by
IDs. Each record stores `completed`, `attempts`, `victories` and nullable
`best_seconds`. Records for uninstalled levels are retained; adding levels does
not reset progress.

## Stable identity

Level definitions include an `id` independent of filename, display name and scene
reference. Shipped levels have permanent UUIDs. Editor assigns an ID when saving
a legacy level and preserves it on later saves. Keep that ID when renaming/moving
a level to retain its records.

For an independent copy, use **Level Definition > Assign New Level ID**, then save
the copy. Save As normally keeps identity, which is useful when moving a level.
Duplicate installed IDs are reported as unavailable in discovery to prevent menu
play from mixing records. Assign the independent copy a new ID or remove the
obsolete copy. IDs support ASCII letters, digits, hyphens and underscores, up to
128 characters.

Game never inserts IDs into original files. Legacy levels remain playable with a
menu notice that tracking requires saving them in Editor. No progression is
associated with filenames or display names.

## Attempts, timing and results

A successfully initialized level starts an attempt, updates last played and saves
those counters immediately. Restart starts another attempt. Returning to menus or
closing mid-level leaves the attempt counted without a victory. Invalid loads do
not count. Completing every wave with the objective alive records exactly one
victory, marks completion and saves. Defeat changes neither victories nor best
time; its attempt is already durable.

Time uses gameplay delta time, including Preparation and Battle. Loading, menus,
Pause/settings and result screens do not count. Restart resets the timer. The first victory
establishes a record; only a strictly faster replay improves it. Runtime and store
guards prevent repeated result frames from duplicating victories or saves.

Selection displays completion, best time and attempt/victory counts, highlights
last played in gold and initially selects its current path by ID. All available
levels remain replayable. Victory shows name, time, waves completed, record status,
**Retry**, **Next Level** when available, and **Choose Level**. Defeat shows name and
wave reached with Retry and Choose Level. Next Level follows the existing
alphabetical-name/path ordering refreshed on entering results; nothing is unlocked
or locked.

## Save safety and architecture

Missing saves start empty. Loading validates the entire document before accepting
records, including counter consistency and finite nonnegative best times.
Corrupted files and unsupported versions remain unchanged, with persistence
disabled for that application session. Gameplay stays available and shows a warning
with details on hover. Session-only records may still be displayed, but cannot
persist while saving is disabled. Back up/restore the file or move it aside while Game is
closed, then reopen Game. This first format does not invent migrations for unknown
older/future schemas.

Writes use a temporary file beside the save followed by atomic replacement
(MoveFileEx with replacement and write-through on Windows). Failed writes leave
the previous save intact and report an error. Changes since loading/last writing
also block replacement rather than knowingly overwriting another session's data.
A leftover `.tmp` is not loaded automatically. Only statistics are saved, with no
authored files or mid-battle state involved.

`runtime/player_progress.*` owns persistence in GameLogic. Game supplies the SDL
user-data path and explicitly attaches the store to LevelRuntime. Editor previews
never attach one. GameHud reads records and reuses existing lifecycle actions.

`Game.Progress` tests reopening, replay, defeats, abandoned attempts, records,
renaming, corrupt/unsupported saves, external changes and write failures.
`Game.LevelMenu` tests victory recording, timing boundaries, duplicate prevention,
Next Level and durable records through actual gameplay transitions.

Debug and Release builds include both Game and Editor. Full suites passed 37/37
in each configuration (259.16 seconds Debug, 124.68 seconds Release), with Vulkan
validation enabled. No compiler warnings or Vulkan validation errors were detected.
Modified C++ files pass clang-format and Git whitespace checks. Logs are in
`out/archive/phase32/`.
