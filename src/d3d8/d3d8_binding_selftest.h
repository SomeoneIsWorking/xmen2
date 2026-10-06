#ifndef D3D8_BINDING_SELFTEST_H
#define D3D8_BINDING_SELFTEST_H

/* Drive SetStreamSource, SetIndices and SetTexture through the production
   device vtable and check the references the device holds on what it binds. */
int d3d8_binding_selftest(void);

#endif /* D3D8_BINDING_SELFTEST_H */
