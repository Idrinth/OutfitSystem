# Idrinth's Outfit System

A lightweight **SKSE plugin** for Skyrim that automatically swaps an NPC's
outfit between a "civilian" and a "military" set depending on where they are —
for example, casual clothes in town and armor out in the wild.

Unlike many Papyrus-based solutions, it does **not** magically conjure new
items every time the outfit switches. Each NPC is given their gear once and the
plugin only ever equips items they already carry. It was built for a follower
mod, but it works for any unique NPC and is significantly faster than a Papyrus
implementation.

## Features

- Automatic outfit switching based on the NPC's current location.
- NPCs wear their **civilian** set in towns/inns/temples and their
  **military** set elsewhere (and always when in combat).
- Items are added to the NPC **once**, never duplicated on every switch.
- Graceful fallback: if the preferred set isn't in the inventory, the other
  set is used instead. Missing/unloadable items are simply skipped.
- Plays nicely with undress mods — it detects **OSA** and **Bath** (Devious
  undress) effects and steps aside while they're active.
- Configured entirely through simple YAML files — no scripting required.

## Requirements

- A working **SKSE64** install matching your Skyrim version.
- **Address Library for SKSE Plugins** (the plugin uses it for cross-version
  compatibility).
- Your NPCs must be **unique** actors (followers, named NPCs, etc.). Generic
  leveled/non-unique NPCs are ignored by design.

## Installation

1. Install with your mod manager (Vortex / Mod Organizer 2) like any other SKSE
   plugin, or copy the files manually into your `Data` folder.
2. After installation you should have:
   - `Data/SKSE/Plugins/IdrinthOutfitSystem.dll`
   - `Data/SKSE/Plugins/IdrinthOutfitSystem.yaml` (global settings)
3. Add one or more configuration files (see below) describing your NPCs.

## Configuration

There are two kinds of configuration files.

### 1. Global settings

`Data/SKSE/Plugins/IdrinthOutfitSystem.yaml` controls the plugin itself.
Currently it only sets the log level:

```yaml
logLevel: info
```

Valid values: `trace`, `debug`, `info`, `warn`/`warning`, `err`/`error`,
`critical`/`crit`, `off`. Defaults to `error` if the file is missing or the
value is invalid.

### 2. NPC outfit configs

Drop one or more `.yml` / `.yaml` files into:

```
Data/SKSE/Plugins/IdrinthOutfitSystem/
```

Every file in that folder (including subfolders) is loaded automatically when
the game starts, so you can split your NPCs across as many files as you like.

#### Example

```yaml
# Locations that count as "civilian" — NPCs wear their civilian set here.
civilianLocations:
  - LocTypeCity
  - LocTypeInn
  - LocTypeTemple

npcs:
  - modName: IdrinthThalui.esp   # the plugin/ESP the NPC comes from
    formId: 0x803                # the NPC's FormID (hex)
    # DEPRECATED: prefer `provide: false` on the relevant piece (see below).
    ignoredEditorIDs:
      - SomeArmorEditorID
    outfits:
      - military:
          modName: IdrinthThalui.esp
          formId: 0xA7BF2
        civilian:
          modName: IdrinthThalui.esp
          formId: 0x4C5CDE
```

#### Field reference

| Field | Where | Description |
| --- | --- | --- |
| `civilianLocations` | top level | List of location **keywords** (e.g. `LocTypeCity`). An NPC is treated as civilian when their current location — or any parent location — has one of these keywords. |
| `npcs` | top level | The list of NPCs to manage. |
| `modName` | npc / outfit | The ESP/ESM/ESL the form comes from, e.g. `IdrinthThalui.esp`. |
| `formId` | npc / outfit | The form's ID in hexadecimal, e.g. `0x803`. |
| `ignoredEditorIDs` | npc | **Deprecated** — being replaced by the `provide` flag and may be removed in a future version. Optional list of armor EditorIDs that should never be auto-added to this NPC. Prefer setting `provide: false` on the relevant outfit piece instead. |
| `outfits` | npc | One or more `military` / `civilian` armor pairs. |
| `military` / `civilian` | outfit | An armor reference (`modName` + `formId`) for each context. |
| `provide` | military / civilian | Optional, defaults to `true`. Set to `false` to stop the plugin from adding that piece to the NPC's inventory (it will still be equipped if the NPC already owns it). |

> **Tip:** FormIDs are written in hex (`0x...`). You can leave out the load
> order prefix — the plugin resolves the form via the `modName` you provide.

## How it works

- The plugin listens to combat, equip, location-change, cell-load and magic
  effect events for the NPCs you configured.
- When an NPC is **in combat**, or **not** in a civilian location, it equips
  the **military** set; otherwise it equips the **civilian** set.
- The first time it sees a managed NPC, it clears their default/sleep outfit
  and adds the configured armor pieces (respecting `provide`, and the
  deprecated `ignoredEditorIDs`).
- If an undress effect is active (OSA / Bath factions or the
  `dz_undress_common.esp` effect), the plugin unequips its managed pieces and
  lets the other mod do its thing.

## Troubleshooting

- **Nothing changes:** make sure the NPC is *unique*, the `modName`/`formId`
  values are correct, and your config file is in the
  `Data/SKSE/Plugins/IdrinthOutfitSystem/` folder.
- **Check the log:** set `logLevel: debug` (or `trace`) in
  `IdrinthOutfitSystem.yaml` and look at the SKSE log, usually under
  `Documents/My Games/Skyrim Special Edition/SKSE/IdrinthOutfitSystem.log`.
  It reports forms it couldn't find and configs it couldn't parse.
- **An item won't equip:** confirm the form is actually an armor and that the
  NPC owns it (or that `provide` isn't set to `false`).

## Building from source

This is a CommonLibSSE-NG SKSE plugin built with CMake and vcpkg.

```powershell
# from the repository root
./cmake/build.ps1
```

The build produces `IdrinthOutfitSystem.dll` and packages it (together with the
default `IdrinthOutfitSystem.yaml`) into a `.zip` ready for distribution.

## License

Released under the [MIT License](LICENSE).

## Feedback

Found a case that isn't handled, or have an idea? Open an issue on the
[project repository](https://github.com/Idrinth/IdrinthOutfitSystem) — feedback
is welcome!
