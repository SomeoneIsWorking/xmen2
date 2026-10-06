---
id: I060
kind: instrument
status: trusted
created: 2026-08-15
---

## Instrument

X2_GPU_DEBUG / gpu_device.c validation announcement

## Validated by

A live backtrace of a driven run showed libVkLayer_khronos_validation.so's queue thread in the process while SDL_CreateGPUDevice's debug_mode was hardcoded true, so every draw was validated and no output said so. The flag is now read from X2_GPU_DEBUG and the state is PRINTED either way (verified: 'Vulkan validation is off (X2_GPU_DEBUG=unset)' with the variable unset).

## Known failure modes

(none recorded yet)
