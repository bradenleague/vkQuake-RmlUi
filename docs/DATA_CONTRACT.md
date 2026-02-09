# Data Contract: Engine-to-UI Binding System

This document describes how data flows between the vkQuake engine (C) and the
RmlUI interface layer (C++), covering game state, cvar two-way sync,
notifications, and UI-to-engine commands.

---

## System Overview

```
 QUAKE ENGINE (C)                          RMLUI LAYER (C++)
 ================                          =================

 cl.stats[MAX_CL_STATS]  ──┐
 cl.items       ─┤                    ┌─── "game" data model ───── RML docs
 cl.time        ─┤── UI_Sync*() ─────>│
 cl.gametype    ─┤   (each frame)     ├─── NotificationModel ─────  {{ centerprint }}
 cl.maxclients  ─┘                    │                              {{ notify_0..3 }}
                                      │
 Cvar_*()  <────── ICvarProvider ─────┤── "cvars" data model ────  <input data-value>
                                      │
 Cbuf_*()  <────── ICommandExecutor ──┘── MenuEventHandler ──────  onclick actions
```

There are **two data models** and **two port interfaces**:

| Name             | RmlUI Model | Direction       | Purpose                          |
|------------------|-------------|-----------------|----------------------------------|
| GameState        | `"game"`    | Engine -> UI    | Health, ammo, weapons, scores    |
| CvarBinding      | `"cvars"`   | Engine <-> UI   | Settings sliders, toggles, enums |
| ICvarProvider    | (port)      | Read/write cvars | Abstraction over Cvar_*()       |
| ICommandExecutor | (port)      | Execute commands | Abstraction over Cbuf_*()       |

---

## Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         QUAKE ENGINE (C, gnu11)                         │
│                                                                         │
│  cl.stats[]      cl.items       Cvar system       Cbuf command buffer   │
│  (256 ints)      (32-bit        Cvar_*()          Cbuf_AddText()        │
│                   bitfield)                        Cbuf_InsertText()    │
└──────────┬──────────┬───────────────┬──────────────────┬────────────────┘
           │          │               │                  │
           │ extern "C" boundary      │                  │
           │ (#ifdef USE_RMLUI)       │                  │
           │          │               │                  │
┌──────────▼──────────▼───────────────▼──────────────────▼──────────────┐
│                    ui_manager.h  (public C API)                        │
│                                                                        │
│  UI_SyncGameState()        UI_SyncScoreboard()                         │
│  UI_NotifyCenterPrint()    UI_NotifyPrint()                            │
│  UI_SyncSaveSlots()        UI_SyncVideoModes()                         │
│  UI_OnKeyCaptured()                                                    │
└──────────┬──────────────────────────┬──────────────────────────────────┘
           │                          │
┌──────────▼──────────────┐  ┌────────▼────────────────────────────────┐
│  types/ (header-only)   │  │  internal/ (C++ implementation)          │
│                         │  │                                          │
│  game_state.h           │  │  game_data_model     ── "game" model     │
│  notification_state.h   │  │  notification_model   ── on "game" model │
│  cvar_schema.h          │  │  cvar_binding         ── "cvars" model   │
│  cvar_provider.h        │  │  menu_event_handler   ── action dispatch │
│  command_executor.h     │  │  quake_cvar_provider  ── ICvarProvider   │
│  input_mode.h           │  │  quake_command_executor─ ICommandExecutor│
└─────────────────────────┘  └──────────────────────────────────────────┘
           │                          │
           │         RmlUI data binding engine
           │                          │
┌──────────▼──────────────────────────▼─────────────────────────────────┐
│                     RML Documents (ui/rml/)                           │
│                                                                       │
│  <body data-model="game">      <body data-model="cvars">              │
│    {{ health }}                   <input data-value="sensitivity">    │
│    data-if="has_quad"             data-event-change="cvar_changed()"  │
│    data-class-low="health<25"                                         │
└───────────────────────────────────────────────────────────────────────┘
```

---

## Data Flow 1: Game State (Engine -> UI)

Per-frame, one-directional push of gameplay data into the HUD.

### Call Chain

```
 ENGINE (sbar.c)                    C/C++ BOUNDARY                     RMLUI
 ===============                    ==============                     =====

 SCR_UpdateScreen()
       │
       ▼
 UI_SyncGameState(                  ui_manager.cpp
   stats, stats_count,     ──────>  GameDataModel_SyncFromQuake()
   items, intermission,              │
   gametype, maxclients,             │  Decode stats[] indices
   level_name, map_name,             │  Unpack item bitflags
   game_time)                        │  Compute armor_type, face_index
                                          │  Detect game mode (dm/coop/sp)
                                          │
                                          ▼
                                    g_game_state (global struct)
                                          │
                                          │  GameDataModel::Update()
                                          │  (dirty variable tracking)
                                          │
                                          ▼
                                    model_handle.DirtyVariable("health")
                                    model_handle.DirtyVariable("armor")
                                    ...only changed fields marked dirty...
                                          │
                                          ▼
                                    RmlUI re-evaluates {{ health }},
                                    data-if="armor > 0", etc.
```

### Stat Index Mapping

The engine passes `cl.stats[]` (array size `MAX_CL_STATS`, currently 256).
The UI layer consumes the classic HUD-related indices below:

```
 Index   Constant            GameState Field
 ─────   ─────────           ───────────────
  0      STAT_HEALTH         health
  2      STAT_WEAPON         (weapon model — not used directly)
  3      STAT_AMMO           ammo
  4      STAT_ARMOR          armor
  6      STAT_SHELLS         shells
  7      STAT_NAILS          nails
  8      STAT_ROCKETS        rockets
  9      STAT_CELLS          cells
 10      STAT_ACTIVEWEAPON   active_weapon (bitflag)
 11      STAT_TOTALSECRETS   total_secrets
 12      STAT_TOTALMONSTERS  total_monsters
 13      STAT_SECRETS        secrets
 14      STAT_MONSTERS       monsters
```

### Item Bitflag Decoding

The `items` parameter is a 32-bit field. Decoded into bool fields:

```
 Bit(s)   Constant              GameState Field
 ──────   ────────              ───────────────
 0        IT_SHOTGUN        (1) has_shotgun
 1        IT_SUPER_SHOTGUN  (2) has_super_shotgun
 2        IT_NAILGUN        (4) has_nailgun
 3        IT_SUPER_NAILGUN  (8) has_super_nailgun
 4        IT_GRENADE_LAUNCHER   has_grenade_launcher
 5        IT_ROCKET_LAUNCHER    has_rocket_launcher
 6        IT_LIGHTNING          has_lightning_gun

 13       IT_ARMOR1  (8192)     armor_type = 1 (green)
 14       IT_ARMOR2  (16384)    armor_type = 2 (yellow)
 15       IT_ARMOR3  (32768)    armor_type = 3 (red)

 17       IT_KEY1  (131072)     has_key1 (silver)
 18       IT_KEY2  (262144)     has_key2 (gold)

 19       IT_INVISIBILITY       has_invisibility
 20       IT_INVULNERABILITY    has_invulnerability
 21       IT_SUIT               has_suit
 22       IT_QUAD               has_quad

 28       IT_SIGIL1             has_sigil1
 29       IT_SIGIL2             has_sigil2
 30       IT_SIGIL3             has_sigil3
 31       IT_SIGIL4             has_sigil4
```

### Derived / Computed Values

Some bindings are not direct struct fields but lambdas (`BindFunc`):

```
 Computed Binding      Source                    Output
 ────────────────      ──────                    ──────
 weapon_label          active_weapon bitflag     "SHOTGUN", "ROCKET L.", etc.
 ammo_type_label       active_weapon bitflag     "SHELLS", "NAILS", etc.
 is_axe                active_weapon == 4096     bool (hide ammo counter)
 is_shells_weapon      active_weapon ∈ {1,2}     bool (highlight shells)
 is_nails_weapon       active_weapon ∈ {4,8}     bool (highlight nails)
 is_rockets_weapon     active_weapon ∈ {16,32}   bool (highlight rockets)
 is_cells_weapon       active_weapon == 64       bool (highlight cells)
 chat_active           key_dest == key_message   bool (show chat overlay)
 chat_prefix           chat_team flag            "say:" or "say_team:"
 chat_text             Key_GetChatBuffer()       current chat input
```

### Game Mode Detection

```
 gametype   maxclients   Result
 ────────   ──────────   ──────
 0          1            Single-player (deathmatch=false, coop=false)
 0          >1           Co-op          (deathmatch=false, coop=true)
 1          any          Deathmatch     (deathmatch=true,  coop=false)
```

### Dirty Variable Optimization

`GameDataModel::Update()` compares every field against a cached previous
state. Only changed fields call `DirtyVariable()`. When `active_weapon`
changes, all weapon-related computed bindings are also dirtied.

---

## Data Flow 2: Notifications (Engine -> UI, time-expiring)

Centerprint and notify messages are pushed from the engine and auto-expire.

### Centerprint

```
 ENGINE                        NOTIFICATION MODEL              RML
 ──────                        ──────────────────              ───

 SCR_CenterPrint()
       │
       ▼
 UI_NotifyCenterPrint(text)
       │
       ▼                       NotificationModel::CenterPrint()
                                 │
                                 ├── s_state.centerprint = text
                                 ├── s_state.centerprint_start = realtime
                                 └── s_state.centerprint_expire = realtime + scr_centertime
                                                      (default 2s if cvar <= 0)

                               Update() each frame:
                                 │
                                 ├── visible = !empty && (intermission || realtime < expire)
                                 ├── if visibility changed:
                                 │     DirtyVariable("centerprint")
                                 │     DirtyVariable("centerprint_visible")
                                 └── if intermission_type >= 2:
                                       DirtyVariable("finale_text")  ← char-by-char reveal

 RML bindings:
   {{ centerprint }}             ← full text
   data-if="centerprint_visible" ← auto-hide after expiry
   {{ finale_text }}             ← typewriter effect during finales
```

### Notify Lines (Rolling Ring Buffer)

```
 ENGINE                        NOTIFICATION MODEL              RML
 ──────                        ──────────────────              ───

 Con_Printf() → UI_NotifyPrint(text)
                                 │
                                 ▼
                               NotifyPrint():
                                 slot = notify_head
                                 notify[slot] = { text, realtime }
                                 notify_head = (slot + 1) % 4

                                            ┌────────────────────┐
                  Ring buffer:              │  notify[0]  "..."  │
                  (4 slots, FIFO)           │  notify[1]  "..."  │
                                            │  notify[2]  "..."  │
                                            │  notify[3]  "..."  │
                                            └────────────────────┘

                               Update() each frame:
                                 for each slot i:
                                   visible = !empty && (realtime - time) < con_notifytime
                                   if visibility changed:
                                     DirtyVariable("notify_i")
                                     DirtyVariable("notify_i_visible")

 RML bindings:
   {{ notify_0 }}, {{ notify_1 }}, {{ notify_2 }}, {{ notify_3 }}
   data-if="notify_0_visible"  (per-slot visibility)
```

---

## Data Flow 3: Cvar Two-Way Sync (Engine <-> UI)

Settings menus use bidirectional binding between Quake cvars and UI controls.

### Architecture

```
 QUAKE CVARS                  CVAR BINDING MANAGER              RML MENUS
 ===========                  ====================              =========

 sensitivity = 3.0            CvarBinding: {
 vid_fullscreen = 1             cvar: "sensitivity"    ◄──────► <input type="range"
 scr_style = 2                  ui: "sensitivity"                 data-value="sensitivity"
                                type: Float                       min="1" max="20"
                                min: 1.0, max: 20.0              step="0.5">
                                step: 0.5
                              }
```

### Registration (at init time)

```
 RegisterFloat("sensitivity", "sensitivity", 1.0, 20.0, 0.5)
 RegisterBool("vid_fullscreen", "fullscreen")
 RegisterEnum("scr_style", "hud_style", 3, {"Simple","Classic","Modern"})
 RegisterEnumValues("vid_anisotropic", "aniso", {1,2,4,8,16}, labels)
 RegisterString("_cl_name", "player_name")
```

### Sync Direction: Engine -> UI (on menu open)

```
 Menu opens
    │
    ▼
 CvarBindingManager::SyncToUI()
    │
    │  s_ignore_ui_changes = true     ← suppress feedback loop
    │
    ├── for each registered binding:
    │     │
    │     ├── value = ICvarProvider::GetFloat(cvar_name)
    │     │
    │     ├── switch(type):
    │     │     Float → s_float_values[ui_name] = value
    │     │     Bool  → s_int_values[ui_name] = (int)value
    │     │     Int   → s_int_values[ui_name] = (int)value
    │     │     Enum  → s_int_values[ui_name] = (int)value
    │     │     String→ s_string_values[ui_name] = GetString()
    │     │
    │     └── model_handle.DirtyVariable(ui_name)
    │         model_handle.DirtyVariable(ui_name + "_label")  (if enum)
    │
    └── keep s_ignore_ui_changes for 2 update ticks
        (cleared by NotifyUIUpdateComplete())
```

### Sync Direction: UI -> Engine (on user interaction)

```
 User drags slider / clicks cycle button
    │
    ▼
 RML fires change event
    │
    ├── data-event-change="cvar_changed('sensitivity')"
    │   OR
    ├── data-event-click="cycle_cvar('hud_style', 1)"
    │
    ▼
 MenuEventHandler::ProcessAction()
    │
    ├── "cvar_changed" path:
    │     CvarBindingManager::SyncFromUI("sensitivity")
    │       │
    │       ├── value = s_float_values["sensitivity"]
    │       └── ICvarProvider::SetFloat("sensitivity", value)
    │             └── Cvar_SetValue("sensitivity", 3.5)
    │
    └── "cycle_cvar" path:
          CvarBindingManager::CycleEnum("hud_style", +1)
            │
            ├── current = s_int_values["hud_style"]  (e.g. 2)
            ├── next = (current + 1) % num_values     (e.g. 0)
            ├── s_int_values["hud_style"] = next
            ├── ICvarProvider::SetFloat("scr_style", 0.0)
            └── DirtyVariable("hud_style")
                DirtyVariable("hud_style_label")
```

### Feedback Loop Prevention

```
 SyncToUI()                         UI Change Event
    │                                     │
    ├─ set s_ignore = true                │
    ├─ write all values                   │
    ├─ dirty all variables ──────────> RmlUI updates DOM
    │                                     │
    │                                RmlUI fires change events
    │                                     │
    │                                MenuEventHandler checks:
    │                                  if (ShouldIgnoreUIChange())
    │                                    return;  ← suppressed!
    │                                     │
    ├─ keep suppression window active     │
    └─ UI_Update() calls NotifyUIUpdateComplete() each frame
       until suppression expires          │
```

### Cvar Type Reference

```
 CvarType   Storage            RML Widget               Sync Behavior
 ────────   ───────            ──────────               ─────────────
 Float      float*             <input type="range">     Direct float value
 Bool       int* (0/1)         Toggle button            Cast to int
 Int        int*               <input type="range">     Cast to int
 Enum       int* (index)       Cycle button + label     Index into labels[]
 String     Rml::String*       <input type="text">      GetString/SetString
```

### Special Cvar Handling

```
 m_pitch (inverted mouse):
   UI reads sign of m_pitch cvar
   invert_mouse = 1 if m_pitch < 0, else 0
   On toggle: negate m_pitch value

 _cl_color (player color):
   Packed byte: top nibble = shirt, low nibble = pants
   Split into cl_color_top (0-13) and cl_color_bottom (0-13)
   On change: recombine and set _cl_color

 vid_width/vid_height (resolution):
   vid_mode_label computed binding: "1920x1080"
   Video mode array synced from engine via UI_SyncVideoModes()
```

---

## Data Flow 4: UI Actions -> Engine (UI -> Engine)

User clicks in menus generate action strings dispatched by MenuEventHandler.

### Dispatch Flow

```
 RML Document                    MenuEventHandler           Engine
 ────────────                    ────────────────           ──────

 <button onclick="navigate('options')">
    │
    ▼
 RML click event fires
    │
    ▼
 MenuEventHandler::ProcessEvent()
    │
    ├── extract action from:
    │     data-event-click  (preferred)
    │     onclick           (RmlUI inline)
    │     data-action       (fallback)
    │
    ├── split on ';' for multi-action:
    │     "close(); command('map e1m1')"
    │     → ["close()", "command('map e1m1')"]
    │
    └── ExecuteAction(each)
          │
          ├── parse: func_name('arg')
          │
          └── dispatch:
                │
                ├─ navigate('path')
                │    └─► UI_PushMenu("menus/path") → load + show doc, set MENU_ACTIVE
                │
                ├─ command('cmd')
                │    └─► ICommandExecutor::Execute("cmd\n") → Cbuf_AddText()
                │
                ├─ cvar_changed('name')
                │    └─► CvarBindingManager::SyncFromUI() → Cvar_SetValue()
                │
                ├─ cycle_cvar('name', delta)
                │    └─► CvarBindingManager::CycleEnum() → Cvar_SetValue()
                │
                ├─ close()
                │    └─► UI_PopMenu() → pop stack, maybe set INPUT_INACTIVE
                │
                ├─ close_all()
                │    └─► UI_CloseAllMenus() → clear stack, set INPUT_INACTIVE
                │
                ├─ quit()
                │    └─► Cbuf_AddText("quit\n")
                │
                ├─ new_game()
                │    └─► close menus, then "map start\n"
                │
                ├─ load_game('slot')
                │    └─► Cbuf_AddText("load s3\n")
                │
                ├─ save_game('slot')
                │    └─► Cbuf_AddText("save s3\n")
                │
                ├─ bind_key('action')
                │    └─► enter key capture mode (see below)
                │
                ├─ connect_to('elem')
                │    └─► read text input, Cbuf_AddText("connect addr\n")
                │
                ├─ host_game('elem')
                │    └─► read text input, Cbuf_AddText("map mapname\n")
                │
                └─ load_mod('elem')
                     └─► read text input, Cbuf_AddText("game modname\n")
```

### Key Capture Flow (for rebinding)

```
 Menu click                    MenuEventHandler              Engine
 ──────────                    ────────────────              ──────

 bind_key('+forward')
    │
    ▼
 s_capturing_key = true
 s_key_action = "+forward"
 (UI shows "Press a key...")
    │
    │     User presses 'W'
    │           │
    │           ▼
    │     in_sdl2.c intercepts key
    │     UI_IsCapturingKey() → true
    │     UI_OnKeyCaptured(key, "w")
    │           │
    │           ▼
    │     MenuEventHandler::OnKeyCaptured()
    │       ├── s_capturing_key = false
    │       ├── Cbuf_AddText("bind \"w\" \"+forward\"\n")
    │       └── CvarBindingManager::UpdateKeybind("+forward", "w")
    │                └── dirty keybind display in UI
    │
    ▼
 UI updates keybind label to show "W"
```

---

## Port Interfaces

The C++ layer accesses engine functionality through two abstract interfaces,
enabling testability and clean separation:

### ICvarProvider

```
 ┌──────────────────────────────────────────────────┐
 │  ICvarProvider (types/cvar_provider.h)           │
 │                                                  │
 │  + GetFloat(name) : float                        │
 │  + GetString(name) : string                      │
 │  + SetFloat(name, value)                         │
 │  + SetString(name, value)                        │
 │  + Exists(name) : bool                           │
 └────────────────────┬─────────────────────────────┘
                      │
                      │ implements
                      │
 ┌────────────────────▼────────────────────────────┐
 │  QuakeCvarProvider (singleton)                  │
 │                                                 │
 │  GetFloat()  → extern "C" Cvar_VariableValue()  │  ⚠ returns double!
 │  GetString() → extern "C" Cvar_VariableString() │
 │  SetFloat()  → extern "C" Cvar_SetValue()       │  takes float
 │  SetString() → extern "C" Cvar_Set()            │
 │  Exists()    → extern "C" Cvar_FindVar()        │
 └─────────────────────────────────────────────────┘
```

### ICommandExecutor

```
 ┌─────────────────────────────────────────────────┐
 │  ICommandExecutor (types/command_executor.h)    │
 │                                                 │
 │  + Execute(command)          (queued)           │
 │  + ExecuteImmediate(command) (front of queue)   │
 └────────────────────┬────────────────────────────┘
                      │
                      │ implements
                      │
 ┌────────────────────▼────────────────────────────┐
 │  QuakeCommandExecutor (singleton)               │
 │                                                 │
 │  Execute()          → Cbuf_AddText(cmd + "\n")  │
 │  ExecuteImmediate() → Cbuf_InsertText(cmd+"\n") │
 └─────────────────────────────────────────────────┘
```

---

## Struct Array Bindings

Some data is synced as arrays of structs for `data-for` iteration in RML:

### PlayerInfo (scoreboard)

```
 Engine                          GameState                    RML
 ──────                          ─────────                    ───

 UI_SyncScoreboard(              players: vector<PlayerInfo>
   ui_player_info_t[], count)      .name        : string     {{ player.name }}
                                   .frags       : int        {{ player.frags }}
                                   .top_color   : int        player shirt color
                                   .bottom_color: int        player pants color
                                   .ping        : int        {{ player.ping }}
                                   .is_local    : bool       highlight local player

 Usage:  <div data-for="player : players">
           <span>{{ player.name }}</span>
           <span>{{ player.frags }}</span>
         </div>
```

### SaveSlotInfo (load/save menus)

```
 Engine                          GameState                    RML
 ──────                          ─────────                    ───

 UI_SyncSaveSlots(               save_slots: vector<SaveSlotInfo>
   ui_save_slot_t[], count)        .slot_id     : string     "s0".."s19"
                                   .description : string     "e1m3 The Necropolis"
                                   .slot_number : int        0-19
                                   .is_loadable : bool       has valid .sav file

 Usage:  <div data-for="slot : save_slots">
           <button data-event-click="load_game('{{ slot.slot_id }}')">
             {{ slot.description }}
           </button>
         </div>
```

### VideoModeInfo (resolution picker)

```
 Engine                          CvarBindingManager           RML
 ──────                          ──────────────────           ───

 UI_SyncVideoModes(              video_modes: vector<VideoModeInfo>
   ui_video_mode_t[], count)       .width      : int
                                   .height     : int
                                   .label      : string      "1920x1080"
                                   .is_current : bool        highlight current

 vid_mode_label computed binding: "1920x1080" (current resolution)
```

### KeybindInfo (controls menu)

```
 CvarBindingManager              keybinds: vector<KeybindInfo>
                                   .action      : string     "+forward"
                                   .label       : string     "Move Forward"
                                   .key_name    : string     "w" / "MOUSE1" / "---"
                                   .section     : string     "Movement"
                                   .is_header   : bool       section header row
                                   .is_capturing: bool       waiting for keypress
```

---

## Complete Binding Reference

### "game" Data Model

All bindings available to `<body data-model="game">`:

```
 DIRECT BINDINGS                TYPE     SOURCE
 ───────────────                ────     ──────
 health                         int      stats[STAT_HEALTH]
 armor                          int      stats[STAT_ARMOR]
 ammo                           int      stats[STAT_AMMO]
 active_weapon                  int      stats[STAT_ACTIVEWEAPON]
 shells                         int      stats[STAT_SHELLS]
 nails                          int      stats[STAT_NAILS]
 rockets                        int      stats[STAT_ROCKETS]
 cells                          int      stats[STAT_CELLS]
 monsters                       int      stats[STAT_MONSTERS]
 total_monsters                 int      stats[STAT_TOTALMONSTERS]
 secrets                        int      stats[STAT_SECRETS]
 total_secrets                  int      stats[STAT_TOTALSECRETS]
 has_shotgun                    bool     items & IT_SHOTGUN
 has_super_shotgun              bool     items & IT_SUPER_SHOTGUN
 has_nailgun                    bool     items & IT_NAILGUN
 has_super_nailgun              bool     items & IT_SUPER_NAILGUN
 has_grenade_launcher           bool     items & IT_GRENADE_LAUNCHER
 has_rocket_launcher            bool     items & IT_ROCKET_LAUNCHER
 has_lightning_gun              bool     items & IT_LIGHTNING
 has_key1                       bool     items & IT_KEY1
 has_key2                       bool     items & IT_KEY2
 has_invisibility               bool     items & IT_INVISIBILITY
 has_invulnerability            bool     items & IT_INVULNERABILITY
 has_suit                       bool     items & IT_SUIT
 has_quad                       bool     items & IT_QUAD
 has_sigil1                     bool     items & IT_SIGIL1
 has_sigil2                     bool     items & IT_SIGIL2
 has_sigil3                     bool     items & IT_SIGIL3
 has_sigil4                     bool     items & IT_SIGIL4
 armor_type                     int      derived (0-3)
 intermission                   bool     from param
 intermission_type              int      from param (0-3)
 deathmatch                     bool     gametype != 0
 coop                           bool     gametype==0 && maxclients>1
 level_name                     string   from param
 map_name                       string   from param
 time_minutes                   int      game_time / 60
 time_seconds                   int      game_time % 60
 face_index                     int      health tier (0-4)
 face_pain                      bool     recent damage
 players                        array    from UI_SyncScoreboard()
 num_players                    int      player count
 save_slots                     array    from UI_SyncSaveSlots()

 COMPUTED BINDINGS (BindFunc)   TYPE     LOGIC
 ─────────────────────────────  ────     ─────
 weapon_label                   string   active_weapon → display name
 ammo_type_label                string   active_weapon → ammo type name
 is_axe                         bool     active_weapon == 4096
 is_shells_weapon               bool     active_weapon ∈ {1, 2}
 is_nails_weapon                bool     active_weapon ∈ {4, 8}
 is_rockets_weapon              bool     active_weapon ∈ {16, 32}
 is_cells_weapon                bool     active_weapon == 64
 chat_active                    bool     key_dest == key_message
 chat_prefix                    string   "say:" or "say_team:"
 chat_text                      string   current chat input buffer

 NOTIFICATION BINDINGS          TYPE     LOGIC
 ─────────────────────────────  ────     ─────
 centerprint                    string   current centerprint text
 centerprint_visible            bool     !empty && (intermission || !expired)
 finale_text                    string   char-by-char reveal (intermission 2/3)
 notify_0 .. notify_3           string   ring buffer text
 notify_0_visible .. notify_3_visible  bool  !empty && within con_notifytime

 EVENT CALLBACKS                TRIGGER
 ─────────────────              ───────
 load_slot(slot_id)             data-event-click on save slot
 save_slot(slot_id)             data-event-click on save slot
```

### "cvars" Data Model

All bindings available to `<body data-model="cvars">`. Registered dynamically
by `CvarBindingManager::RegisterAllBindings()`:

```
 UI NAME             CVAR                TYPE    RANGE/VALUES
 ───────             ────                ────    ────────────
 sensitivity         sensitivity         Float   1.0 - 20.0, step 0.5
 fov                 fov                 Float   50 - 130, step 5
 gamma               gamma               Float   0.5 - 2.0, step 0.05
 volume              volume              Float   0.0 - 1.0, step 0.05
 bgmvolume          bgmvolume           Float   0.0 - 1.0, step 0.05
 fullscreen          vid_fullscreen      Bool
 vsync               vid_vsync           Bool
 crosshair           crosshair           Enum    {Off, Cross, Dot}
 hud_style           scr_style           Enum    {Simple, Classic, Modern}
 skill               skill               Enum    {Easy, Normal, Hard, Nightmare}
 player_name         _cl_name            String
 ...                 (50+ total)

 ENUM LABEL BINDINGS (auto-generated):
   {{ hud_style_label }}  → "Simple" / "Classic" / "Modern"
   {{ skill_label }}      → "Easy" / "Normal" / "Hard" / "Nightmare"
   {{ vid_mode_label }}   → "1920x1080"
```

---

## Timing & Lifecycle

```
 Engine Boot                          Menu Session
 ===========                          ============

 Host_Init()                             User presses Escape
    │                                        │
    ├─ UI_Init()                             ├─ keys.c decides open vs close:
    │    ├─ Create RmlUI context             │    ├─ if menu open: UI_HandleEscape()
    │    ├─ GameDataModel::Initialize()      │    └─ else: UI_PushMenu(main/pause)
    │    ├─ CvarBindingManager::Initialize() │
    │    └─ MenuEventHandler::Initialize()   ├─ UI_PushMenu("menus/main_menu")
    │                                        │    ├─ load RML document
    ├─ UI_InitializeVulkan()  ← cvars        │    ├─ RegisterWithDocument()
    │   still have defaults!                 │    ├─ set MENU_ACTIVE
    │                                        │    └─ CvarBindingManager::SyncToUI()
    ├─ exec quake.rc ← configs load          │         └─ read all cvars → UI storage
    │                                        │
    │                                        ├─ (user interacts with menu)
    │                                        │    ├─ change events → SyncFromUI()
    │                                        │    └─ click events → ProcessAction()
    │                                        │
 Each Frame                                  ├─ User clicks "Close"
 ==========                                  │    ├─ close_all()
 SCR_UpdateScreen()                          │    └─ stack drains → INPUT_INACTIVE
    │
    ├─ UI_SyncGameState() ← push stats
    ├─ UI_Update()
    │    ├─ GameDataModel::Update() ← dirty tracking
    │    └─ NotificationModel::Update() ← expiry checks
    └─ UI_Render()
```

**Key timing note:** `UI_InitializeVulkan()` fires BEFORE `exec quake.rc`,
so cvars still hold defaults at init time. The `SyncToUI()` call at
menu-open time reads the correct post-config values.

---

## Files Reference

```
 types/ (header-only, no framework deps)
 ├── game_state.h           GameState, PlayerInfo, SaveSlotInfo, etc.
 ├── notification_state.h   NotificationState, NotifyLine
 ├── cvar_schema.h          CvarType, CvarBinding
 ├── cvar_provider.h        ICvarProvider interface
 ├── command_executor.h     ICommandExecutor interface
 └── input_mode.h           ui_input_mode_t enum

 internal/ (C++ implementation)
 ├── game_data_model.h/cpp       "game" data model (50+ bindings)
 ├── notification_model.h/cpp    Centerprint + notify (on "game" model)
 ├── cvar_binding.h/cpp          "cvars" data model (two-way sync)
 ├── menu_event_handler.h/cpp    Action string dispatch
 ├── quake_cvar_provider.h/cpp   ICvarProvider → Cvar_*() bridge
 └── quake_command_executor.h/cpp ICommandExecutor → Cbuf_*() bridge

 root
 └── ui_manager.h/cpp            Public extern "C" API for engine
```
