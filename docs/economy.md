# Economy and construction costs

Load First Watch using **Navigation > Level > Load First Watch**. The **Level**
gameplay panel shows current gold, tower costs and affordability, and the last
completed wave's reward. Select a tower there and click **Build Tower** for paid
construction during preparation. World-space previews, build zones, footprints,
obstacles and required navigation routes retain their existing checks. When the
balance cannot cover the selected type, construction is disabled with an
**Insufficient gold** message. Continuous placement also turns red once funds run
out. Escape cancels without spending.

The separate **Navigation > Towers** tools are explicitly labeled **Editor
authoring: free**. They place or modify authored towers without changing gameplay
gold, including while inspecting a battle. They still enforce spatial and route
validation. This is an editor authoring path, not a gameplay purchase or refund.
Removing a tower never awards gold. Normal Inspector edits also never charge gold.

## Configuration

Level JSON retains version `1` and adds a nonnegative integer `starting_gold` at
the root. Each predefined wave adds its own fixed nonnegative integer
`gold_reward`:

```json
{
  "starting_gold": 100,
  "waves": [
    {"name": "Morning visitors", "gold_reward": 25, "groups": []}
  ]
}
```

This fragment illustrates the economy fields; real waves still need their explicit
enemy groups. See `assets/levels/first_watch.json` for a complete level.

Each entry in `assets/config/towers.json` adds `construction_cost`, for example:

```json
{"id": "basic", "construction_cost": 30}
```

Keep the existing name, model, collision dimensions, scale and tint fields. First
Watch starts with **100 gold**; Basic costs **30**, Wide costs **50**, and the three
authored wave rewards are **25**, **35**, **50**. There is no income over time, kill
reward, scaling, selling, or procedural reward calculation.

Missing economy fields default to zero so older level and tower catalogs continue
loading. A missing tower cost preserves free construction for that definition.
Gold values must be integers from zero through the unsigned 64-bit maximum;
negative, fractional, boolean, string and null values are rejected. Starting gold
plus all scheduled rewards must fit within that range. Runtime earning checks
overflow before changing the balance.

## Responsibilities and transactions

`EconomySystem` in Game owns the single runtime balance and exposes reset, earning,
affordability and spending operations independently of ImGui, rendering and scene
data. `LevelSession` initializes it from the immutable level configuration and
resets it when loading/restarting a level. Runtime gold is not serialized into
engine scene JSON. Authored towers loaded with a scene do not charge construction
costs again.

Gameplay calls `LevelSession::buildTower`. It checks preparation and affordability,
then asks the existing NavigationSession to validate and commit the tower. Only a
successful commit spends gold. Invalid zones, overlaps, insufficient funds, closed
routes and placement exceptions do not charge currency. The editor explicitly
uses NavigationSession's authoring placement mode instead of this paid entry point.

Wave rewards are applied when WaveSystem advances from a completed wave to the
next preparation phase or final victory. Completion requires every scheduled
enemy to have spawned and the active population to be empty. The previous wave
index identifies the configured reward; later updates cannot pay it again. Defeat
does not advance the wave and awards no reward, even if its last enemies arrived.

`Economy.Cpu` checks transactions, overflow, exact configuration, legacy defaults
and malformed JSON. `Enemies.Runtime` exercises paid construction through the
gameplay panel, failed and unaffordable placements, free authoring, no removal
refund, all three fixed rewards exactly once, final victory, defeat without a
reward, and restart reset with real scenes/navigation.
