---
id: C135
kind: claim
status: holds
created: 2026-08-06
tags: d3d8,graphics,native,resources
---

## Claim

IDirect3DTexture8::GetSurfaceLevel hands back a VIEW on the texture's own staging bytes, refcounted on the texture, and uploading that level is the same code the texture's own UnlockRect runs

## Evidence

src/d3d8/d3d8_resource.c tex_GetSurfaceLevel + d3d8_texture_level_unlocked; src/d3d8/d3d8_surface.c d3d8_surface_new_texlevel; src/d3d8/d3d8_com.c d3d8_object_set_owner. Proved by --d3d8-selftest's texture_level_selftest, driven through the real texture vtable: the level-1 surface's LockRect pointer EQUALS the texture's own level-1 LockRect pointer, its GetDesc reports 32x16 (not the texture's 64x32), and unlocking it raises the texture's completed-upload counter with last_upload_level==1. The test was run against three deliberate mutations and caught all three: (a) the surface given guest_malloc storage of its own -> 'the surface has storage of its own', (b) the surface unlock not uploading -> '1 uploads before and after', (c) the level ignored when sizing -> three failures at once. The real run then cleared the GetSurfaceLevel stop it had been dying on.

## What would falsify it

a level surface whose LockRect pointer differs from the texture's own for the same level, or a texture that is destroyed while the engine still holds one of its level surfaces (the owner refcount is what prevents it)
