# Pickups

A dropped item is a `CInventoryEntity` (vtable `0x00687d0c`) whose model comes
from its item, not its entity definition. This doc owns the drop path, the
ways it leaves a pickup that can be used but is not drawn, and how the port
reports them. `src/native/retail_pickups.{hpp,cpp}` reads the layout below,
`src/native/model_attach_report.cpp` logs the failures, and
`src/native/control_pickups_route.cpp` serves `GET /pickups`.

## Drop path

1. `FUN_0046bd90` (a virtual at `0x00687080`; owner, item, position, ret 0x10)
   picks the entity definition by item kind: `inventory_pickup`, `_rare` or
   `_unique` for equipment, `inventory_money`, else `gen_inventory_touch`
   (`data/item_ents.xmlb`). None of them has a `model` field.
2. The factory `FUN_004612e0` creates it 6 units above the position and
   `FUN_0042be20` casts it. No entity: the item record is dropped, nothing
   appears.
3. `setItem` `FUN_00479cc0` (thiscall owner, item, count; ret 0xc) writes the
   count at `+0x322`, the `"~<colour><name>"` prompt label at `+0x170`
   (`FUN_00479c70`), then, when vt `+0x180` (`FUN_00471600`, a model is
   attached and loaded) is false, copies the item's `model` string
   (`data/items.xmlb`, e.g. `pickups/tech_01`) into a 32-byte buffer and calls
   `setModel(buffer, 2)` if it is not empty. **It returns 1 whatever setModel
   answers.** Only a false return (no item) makes the drop path remove the
   entity (vt `+0xbc`).
4. The drop path then launches it with a random impulse, sets the loop effect
   level and starts its touch/use activation.

Save restore goes through `FUN_00479fc0`, and the item-change path through
`FUN_0047a080`; both call the same `setItem`.

The other setModel caller, `CPhysicalEntity` definition read `FUN_00475510`,
removes an entity whose model fails. Only pickups survive a failed model.

## setModel

`FUN_004730f0` (thiscall path, flags; ret 8; AL = attached), with the
descriptor `FUN_0057a090()` = `0x007dc88c` (vtable `0x0069c4d0`; types 0, 2,
3 in slots 0, 1, 2):

- `path == NULL`: false.
- `FUN_005756e0` refuses while the model manager (`FUN_00575620()` =
  `0x007bf6d0`) is out of instances (`+0x12938` vt `+0x14` =
  `FUN_00575330`: used `0x007da28c` == `0x2b4`) or out of a component the
  descriptor needs (type 0: `+0x2c00` == `0x2b4`, type 2: `+0x80d0` ==
  `0x1b0`, type 3: `+0xe5d4` == 400).
- The path gets `models/` unless it has it, `FUN_00575c90` takes an instance,
  `FUN_00471520` stores it at `entity+0x1cc`.
- vt `+0x178` `FUN_00472f00` loads the scene component (instance slot 2,
  vtable `0x0069c1a4`) through its `+0xc` `FUN_00577c70`: the model cache
  (`FUN_0056f9f0()` = `0x007b7fb0`) `+0x10` `FUN_0056ebe0` returns a cached
  file by resource name, else loads it with `+0x14` `FUN_0056ed00`. That load
  refuses a new file once the cache holds 200 (`+0x73ec`) or the resource name
  table (`FUN_0055af80()` = `*0x007ac244`) holds more than `0x1c1` (`+0x9404`),
  and fails when the file does not open or parse. On success the component
  keeps the cache record at `+8` and its root group at `+4`.
- A failed load releases the instance (vt `+0x17c` `FUN_00471570`) and clears
  `+0x1cc`. Retail logs none of this.

## Failure paths for a usable, undrawn pickup

| Where | Cause | Port log (`model` channel, error) |
|---|---|---|
| setItem | the item has no `model` (the keys and keycards in `items.xmlb`; none of them drop) | `setItem 0x00479cc0: pickup entity H (def, "label") has no model: its item names no model` |
| setModel | the instance pool or a component pool is full | `... has no model: setModel refused "path": the instances pool is full (692 of 692)` |
| setModel | a new file while the model cache (200) or resource name table (`0x1c2`) is full | `... setModel refused "path": the model_files pool is full (...)` |
| setModel | the file is missing or does not load | `... setModel refused "path": the model file did not load` |

A pool is reported full only from its count before the call; when none is,
the refusal is the file. setItem's 32-byte copy would cut a name of 32 or
more characters; the longest shipped item model is 26
(`pickups/health_item_pickup`), so that path is not reached. A setModel
failure outside setItem logs as `setModel 0x004730f0: entity H (def) got no
model for "path": reason`.

What these logs do not cover: a pickup whose model attached and is later
released or culled at draw. `/pickups` shows that case as `loaded: true`
with nothing logged, or as `loaded: false` with nothing logged when something
released it after setItem.

## Checking it

`GET /pickups[?handle=N]` (`tools/x2ctl.py` or curl on the control port),
read at the guest input poll:

- `pools`: used and capacity of everything setModel draws from
  (`instances`, `component0`..`7`, `model_files`, `resource_names`).
- `pickups`: each `CInventoryEntity` with its handle, definition, label,
  position, flags, quality, count, `model` (instance `+0x1cc`, its three
  component slots, the scene component's vtable, cache record `scene_model`
  and root `scene_root`, and `loaded`) and loop effect.
- `without_model`: how many listed pickups are not `loaded`.

When a player sees an item they can use but cannot see, `/pickups` names it
(`loaded: false`) and the log line written when it dropped says why.
`tools/live_case.py pickup-models` spawns and kills Mikhail Rangers in Dead
Zone 2 until something drops (drops are random) and expects every drop
`loaded` with no report. `pickup-models-unloadable` replaces
`models/pickups/tech_0{1,2,3}.igb` with empty files in the run's asset overlay
and expects every money drop `loaded: false` with one `setItem` line each
reading `the model file did not load`.
