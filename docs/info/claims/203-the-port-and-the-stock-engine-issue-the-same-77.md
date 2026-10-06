---
id: C203
kind: claim
status: holds
created: 2026-08-15
tags: d3d8,geometry,vertex-shader,skinning
---

## Claim

The port and the stock engine issue the SAME 77 draws in the same order for the same scene, and differ in exactly two: the character hulls, which the control draws with a VERTEX SHADER (handle 0x003, stride 32) and the port draws as fixed-function FVF 0x002, stride 12, having created no vertex shader at all.

## Evidence

tools/proxy_d3d8 now captures the stock engine's per-draw object-space vertices (F9 at Present) in the same OBJ format the port writes with X2_DRAW_OBJ; tools/objcmp.py compares them by draw signature and its selftest passes on identical, flattened, unmatched and empty inputs. Control scratch/run/stocklog/d3d8_frame.obj: 77 draws, 14380 vertices, prim counts 20/212/32/548/727/.../1582/1479/1870/1595 -- identical in count and order to the port's frame table for the same dialog (scratch/logs/drive.log, frames 555 and 730, 77 draws each, same prim counts). The ONLY signature difference: control draw29 fvf00003_stride32_prims1479 and draw31 fvf00003_stride32_prims1595, against the port's draw29 fvf 0x00002 stride 12 and draw31 fvf 0x00002 stride 12. 0x001 is D3DFVF_RESERVED0, which an FVF may never set and which is how D3D8 marks a shader handle. The port's run reports '0 vertex shader(s) created, 0 deleted, 0 refused'.

## What would falsify it

a run in which the port's new SetVertexShader census shows it receiving a handle with bit 0 set for those draws -- that would move the divergence upstream of D3D8 -- or a scene where the two dumps differ in more than these two draws
