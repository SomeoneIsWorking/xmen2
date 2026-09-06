#ifndef X2_MACOS_BUNDLE_H
#define X2_MACOS_BUNDLE_H

/* Resolve `.../Foo.app` from `.../Foo.app/Contents/MacOS/foo`, or return 0.
   Textual, so it is testable without a bundle on disk. */
int x2_macos_bundle_root(const char *executable, char *root,
                         unsigned capacity);

/* Publish this bundle's UI resources and Vulkan driver into the environment
   and SDL's hints, without overwriting anything already set, and report what
   the bundle is missing. Returns 1 when the executable IS inside a bundle --
   the packaged product launch shape, the same one --appimage names on
   Linux -- and 0 for every other launch, including on other platforms. */
int x2_macos_bundle_init(const char *executable);

#endif /* X2_MACOS_BUNDLE_H */
