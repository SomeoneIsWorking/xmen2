# External provenance and historical references

This document preserves attribution and historical context for external code,
assets, vocabulary, and ideas already evaluated or adapted. It is not an
architecture authority, a design checklist, or a source of current project
requirements. Current ownership comes from `docs/codemap.md`; current behavior
and gaps come from `docs/project-state.md`. Keep exact provenance for material
actually reused.

## Settings-window stylesheet attribution

`assets/ui/settings.rcss` adapts CC0 stylesheet material from
[Dusklight](https://github.com/TwilitRealm/dusklight), specifically
`res/rml/window.rcss` at commit
`0fc05028ccfe809c569b1b84c0bb87f382b0bf34`. The adapted material is the
`window`, `tab-bar`, `tab`, `content`, scrollable `pane`, `select-button`,
`key`, and `value` vocabulary plus the dark/gold focus treatment. This
attribution applies only to that stylesheet material; this project defines its
own architecture, behavior, and ownership boundaries.

## D3D8 implementations — DXVK/d8vk and WineD3D

`src/d3d8/` implements Direct3D 8 on the host. Two mature open implementations
of the same interface exist, and one of them is already in this project's own
loop, so the decision to write ours needs stating rather than assuming.

- **DXVK** — https://github.com/doitsujin/dxvk, **zlib**. D3D8 landed in 2.4,
  merged from **d8vk** (~5k lines) and implemented on top of DXVK's D3D9.
  `dxvk-native` builds without Wine; whether d3d8 is built in the *native*
  configuration is UNVERIFIED here — nobody has checked its meson files.
- **WineD3D** — Wine's `d3d8.dll`, **LGPL**, designed to sit on Wine internals.
- Both are further along than this will be for a long time, and DXVK's d3d8 is
  what actually renders this game today: the Wine oracle (C005) depends on it.

### The decision: read them, do not depend on them

The port exists so that rendering can eventually be *changed* — interpolation,
replaced shaders, effects the 2005 engine never had. Anything sitting behind
someone else's translation layer is capped by what that layer chooses to
expose, and the interesting work is exactly the work that has to reach inside
the pipeline. So these are REFERENCES for semantics — what D3D8 actually
promises for a given state combination, which formats behave how, how
fixed-function maps onto a programmable pipeline — and the code here is ours.

Record exact provenance in the file that adapts material from them.

### What using DXVK would NOT have saved

Worth recording, because it is the part that looks like a shortcut and is not.
DXVK's objects are 64-bit host C++ objects; x86-32 guest code cannot call
one. The boundary in `d3d8_com.cpp` -- 32-bit `__stdcall` vtables in
guest-addressable memory, synthetic callback addresses, this-pointer
resolution, who-pops-what -- would have been needed either way, and it is what
already exists. What DXVK would have replaced is the part still ahead:
resources, state-to-pipeline translation, and vs/ps 1.1 bytecode.

## Title prior art

External work on X-Men Legends II PC `XMen2.exe` (3,129,344 bytes, SHA-256 `146cd9c316edb57a267cd73753a7ce9af647e52aab750d449c6b278fb4a1669b`, PE timestamp `0x43203baf`, image base `0x00400000`; `libIG*.dll` at `0x10000000`), checked against our install with `objdump`.

| project | URL | kind | license | target build | build match (addresses checked) | what we may take | notes |
|---|---|---|---|---|---|---|---|
| XML2-MUA-Display-Mods (`research/OFFSETS-XML2.md`) | https://github.com/KaikoClanworth1/XML2-MUA-Display-Mods | RE notes for resolution, screen mode and window style | MIT | same 3,129,344-byte `XMen2.exe` | matched: `0x005fac45` calls `0x0061bf80` (resolution reader, function start); `0x005facb1` is `mov byte [edi+4],1` (forced fullscreen); `libIGDisplay.dll` `0x1000581a` `mov edi,0x06cf0000` and file offset `0x5809` holds `00 00 00 85` | evidence only (display and window facts) | Created 2026-06. |
| XML2-Modern-Controller | https://github.com/andreMeloo/XML2-Modern-Controller | controller proxy and analog hook DLL with build profile | MIT | SHA-256 identical to ours | matched: profile SHA-256, size, timestamp `0x43203BAF` and image size `0x006744C6` equal our exe; hook addresses not checked | evidence only (input path) | Created 2026-09. |
| xml2-debug-menu | https://github.com/KaikoClanworth1/xml2-debug-menu | ASI ImGui overlay; BehavEd command dispatch tables | none ("private" badge) | PC `XMen2.exe` | matched: `0x004d5830` (BehavEd `GetParam`, `__thiscall`) is a function start after `int3` padding; game-manager global `0x006f38f8` not checked | names only | Created 2026-03. Relevant to the command tables in `docs/RE/boot.md`. |
| MUA.XML2.LimitAdjuster | https://github.com/nikita488/MUA.XML2.LimitAdjuster | limit adjuster (ThirteenAG framework) | MIT | Marvel Ultimate Alliance; XML2 has no profile in `limits/` | not applicable (MUA addresses only) | nothing | Last push 2020. |
| igBlender | https://github.com/ak2yny/igBlender | Blender plugin for Alchemy 5 IGB | GPL-3.0 | asset format | not applicable | evidence only (IGB format) | |
| igb-blender | https://github.com/KaikoClanworth1/igb-blender | Blender IGB/IGZ importer/exporter | none | asset format | not applicable | evidence only | |
| alchemy | https://github.com/SomeoneIsWorking/alchemy | our shared Alchemy layer | ours | asset format | not applicable | ours | |
| xmenlegends (Cwf1997), OpenXML1xbox, legends-classic | https://github.com/Cwf1997/xmenlegends | Xbox X-Men Legends (2004) decomp / recompilation | MIT / none / MIT | X-Men Legends 1 Xbox | not applicable (different game) | nothing | Shares the engine family only. |

Not found (searched 2026-10-13): a decompilation (matching or not) of X-Men Legends II on decomp.dev or GitHub for any platform; a public Ghidra or IDA database or symbol map for `XMen2.exe` or the `libIG*.dll` set; a PC recompilation. Codeberg search returned its anti-scraper page and was not checked.
