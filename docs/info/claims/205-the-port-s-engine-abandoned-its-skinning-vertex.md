---
id: C205
kind: claim
status: holds
created: 2026-08-15
tags: d3d8,caps,vertex-shader,skinning
---

## Claim

The port's engine abandoned its skinning vertex shader because D3DCAPS8::MaxVertexShaderConst declared 96. Raising it to the control's 256 makes the engine create and bind the shader -- 1832 binds in a driven run against 0 before.

## Evidence

Before: scratch/logs/native.log reported '9 distinct SetVertexShader value(s), 0 of them SHADER handles' and '0 vertex shader(s) created' (C204). After raising MaxVertexShaderConst 96 -> 256 in src/d3d8/d3d8_caps.c (and D3D8_MAX_VS_CONSTANTS with it, so the promise and the storage are one number): '10 distinct SetVertexShader value(s), 1 of them SHADER handles: ... 0xf0000101*', 'CreateVertexShader -> 0xf0000101 (5 declaration token(s), 104 bytecode dword(s))', and that handle bound x1832. The engine's decision is therefore taken from this capability bit and nothing else changed. A SECOND 96 then surfaced: src/d3d8/d3d8_vertex_shader.c hardcoded a 96-register constant file in five places, so the engine indexed c[96] on the promise and the executor refused all 1832 draws -- '1832 draw(s) refused for a vertex format this host cannot express', 96753 draws checked vs 94921 submitted. The register file now IS D3D8_MAX_VS_CONSTANTS.

## What would falsify it

a driven run in which the VS executor still runs far fewer draws than the 1832 the shader is bound for, or in which the characters' geometry is unchanged on screen
