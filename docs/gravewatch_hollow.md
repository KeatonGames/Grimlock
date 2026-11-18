# Gravewatch Hollow

The second chapter uses the continuous terrain system with rolling dark green hills, a winding graveyard road, a ruined gate, a clearing and elevated Gravewatch Keep. Six construction zones overlook the route. The visual terrain extends well beyond the playable boundary. All structures and graves remain editable scene objects.

Following the revised enemy request, all five waves use the existing **Skeleton**, including its original model, animations, melee attack and resurrection. There are no archer or armored variants. The final **Skeleton Captain** uses the same model at 1.65 units tall, with 1,050 health, slower movement and the existing boss health bar. Its bones can also be destroyed before resurrection.

The Knight has 240 health, speed 2.8 and the existing automatic melee attack. His shield automatically reduces one incoming hit by 75% every three seconds. It works in strategy and direct control. His sword and shield use existing KayKit assets attached to animated hand sockets. The roster contains one Knight, using slot 1 and the existing portrait/control UI.

Gravewatch starts locked. The level definition's optional `required_level` contains Greenhaven's stable ID; completing Greenhaven unlocks the chapter and its Knight. This is derived from the existing persisted completion record, so no separate unlock save can become inconsistent. Registered locked map locations can be selected to read their requirement; Play remains disabled. Manual map locks continue to work.

The definition is `game/assets/levels/gravewatch_hollow.json`; the scene is `game/assets/scenes/gravewatch_hollow.json`. Editor's level panel exposes **Required level ID**, and saving preserves it. Use `GravewatchAuthor` to regenerate the chapter without touching Level 1:

```powershell
cmake --build --preset debug --target GravewatchAuthor
out/build/debug/Debug/GravewatchAuthor.exe game/assets
```

Run the chapter integration check with:

```powershell
ctest --test-dir out/build/debug -C Debug -R '^Game.Gravewatch$' --output-on-failure
```

The check covers terrain, all construction locations, Skeleton and boss routes and health bar, Knight shield cooldown, direct-control entry/exit, a strategy-only five-wave victory, progress reload, scene/level round trips and a Level 1 load. The accelerated simulation advances enemy animation playback explicitly because animation playback normally uses the rendering clock.
