/*
 * IDirect3D8 -- the object Direct3DCreate8 returns, and the game's entry into
 * DirectX. Sixteen methods, of which the engine uses the adapter queries, the
 * capability query and CreateDevice.
 *
 * This file owns the import override too: it is the one place the guest can
 * enter, so it is the one place the host has to be armed.
 */
#include "d3d8_caps.h"
#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_host.h"
#include "d3d8_resource.h"
#include "d3d8_surface.h"
#include "d3d8_types.h"

#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

/* ---- the adapter ------------------------------------------------------- */

/*
 * The display modes this adapter reports.
 *
 * The base list is the set the game's own options screen offers, at both of
 * the formats a D3D8 title of this era uses. To it adapter_enumeration adds
 * the ONE size this launch published into the game's Display\Resolution
 * value (display_mode_seed), because the engine validates a chosen mode
 * against what the adapter enumerates and the port must not offer an output
 * size its own boundary refuses to name.
 *
 * If the engine is ever seen picking a mode that is not in this list, that is
 * this list being too short -- not the engine being wrong.
 */
static const struct {
  uint32_t w, h;
} g_base_modes[] = {{640, 480},   {800, 600},   {1024, 768},
                    {1152, 864},  {1280, 720},  {1280, 960},
                    {1280, 1024}, {1600, 1200}, {1920, 1080}};
static const uint32_t g_mode_formats[] = {D3DFMT_X8R8G8B8, D3DFMT_R5G6B5};

#define NFORMATS ((int)(sizeof g_mode_formats / sizeof g_mode_formats[0]))
#define NBASE_MODES ((int)(sizeof g_base_modes / sizeof g_base_modes[0]))

/*
 * The extra mode slot: the size this launch published into the game's
 * Display\Resolution value, when it is not already in the base list. Resolved
 * once -- the engine enumerates repeatedly and the answer cannot change
 * mid-run.
 */
static struct {
  int resolved, present;
  uint32_t w, h;
} g_published_mode;

static void resolve_published_mode(void) {
  if (g_published_mode.resolved)
    return;
  g_published_mode.resolved = 1;
  {
    extern uint32_t x2_display_mode_seed_width(void);
    extern uint32_t x2_display_mode_seed_height(void);
    uint32_t w = x2_display_mode_seed_width();
    uint32_t h = x2_display_mode_seed_height();
    int j;
    if (w && h)
      for (j = 0; j < NBASE_MODES; j++)
        if (g_base_modes[j].w == w && g_base_modes[j].h == h)
          return;
    if (w && h) {
      g_published_mode.present = 1;
      g_published_mode.w = w;
      g_published_mode.h = h;
      printf("d3d8: adapter enumerates the published %ux%u as mode "
             "%d\n",
             w, h, NBASE_MODES);
    }
  }
}

static int nmodes(void) {
  resolve_published_mode();
  return NBASE_MODES + (g_published_mode.present ? 1 : 0);
}

#define DESKTOP_FORMAT D3DFMT_X8R8G8B8
#define DESKTOP_HZ 60u

/*
 * The mode the "desktop" is in, which is what the engine compares against
 * when deciding whether a windowed mode is legal. The old constant pair
 * (1280x1024) made every larger windowed mode ILLEGAL here: the engine built
 * the device, failed its own legality pass against this value, and reverted
 * the stored resolution to default ("Display failed!", the #22 path). What
 * the value means is the real desktop, so ask SDL for it; without SDL video
 * there is nothing the engine could compare against anyway and the constant
 * stands.
 */
static void current_desktop(uint32_t *w, uint32_t *h) {
#ifdef X2_WITH_SDL
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    const SDL_DisplayMode *m =
        SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    if (m && m->w > 0 && m->h > 0) {
      /*
       * PIXELS, not logical points. SDL_DisplayMode's w/h are in the
       * desktop's own coordinate space, which on a HiDPI display is the
       * SCALED size: this 4K screen reports 1536x864 with
       * pixel_density 2.5. Handing the guest 1536x864 made the engine's
       * own legality pass refuse every mode above it -- 1920x1080 on a
       * 3840x2160 monitor came back as "too big for the desktop" and
       * the engine silently built an 800x600 device instead, which is
       * indistinguishable from the port failing to ask for the mode at
       * all. The guest is a 2005 D3D8 title with no notion of display
       * scaling, so the only number that means anything to it is the
       * pixel count.
       */
      float density = m->pixel_density > 0.0f ? m->pixel_density : 1.0f;
      *w = (uint32_t)((float)m->w * density + 0.5f);
      *h = (uint32_t)((float)m->h * density + 0.5f);
      return;
    }
  }
#endif
  *w = 1280u;
  *h = 1024u;
}

static void *guest_ptr(uint32_t a, const char *what) {
  if (!a) {
    fprintf(stderr, "d3d8: %s was given a NULL %s\n", d3d8_current_method(),
            what);
    return NULL;
  }
  return guest_memory_pointer(a);
}

/* ---- IUnknown ---------------------------------------------------------- */

/*
 * Reference counting is real, not a stub returning 1 -- the engine's release
 * path walks its objects and drops them, and a count that never reaches zero
 * means the backend never learns the device is gone. It lives in d3d8_com.c
 * so every interface counts the same way.
 */
typedef struct {
  D3D8CapsLimits limits;
} D3D8Ctx;

static D3D8Ctx g_d3d8;
static D3D8Object *g_d3d8_obj;

static void d3d8_QueryInterface(D3D8Object *self, CPU *C) {
  uint32_t ppv = d3d8_arg(C, 1);
  /*
   * IID_IUnknown and IID_IDirect3D8 both answer with this object; anything
   * else is refused. The engine does not call this, so a call arriving here
   * is worth seeing: it means something wants an interface this host has not
   * been asked for before.
   */
  fprintf(stderr, "d3d8: IDirect3D8::QueryInterface -- refusing an interface "
                  "this host does not implement (E_NOINTERFACE). If the "
                  "engine needed it, the next fault will say so.\n");
  if (ppv)
    WR32(ppv, 0);
  d3d8_ret(C, E_NOINTERFACE);
  (void)self;
}

static void d3d8_AddRef(D3D8Object *self, CPU *C) {
  d3d8_ret(C, (uint32_t)d3d8_object_addref(self));
}

static void d3d8_Release(D3D8Object *self, CPU *C) {
  d3d8_ret(C, (uint32_t)d3d8_object_release(self));
}

/* ---- adapters ---------------------------------------------------------- */

static void d3d8_GetAdapterCount(D3D8Object *self, CPU *C) {
  (void)self;
  d3d8_ret(C, 1);
}

static void d3d8_GetAdapterIdentifier(D3D8Object *self, CPU *C) {
  D3DADAPTER_IDENTIFIER8 *id =
      (D3DADAPTER_IDENTIFIER8 *)guest_ptr(d3d8_arg(C, 2), "identifier");
  (void)self;
  if (!id) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  memset(id, 0, sizeof *id);
  /*
   * The name matters. libIGGfx has a driver-quirk database it consults by
   * adapter description (detectDriverDatabaseProperties), and naming a real
   * vendor's card would make it apply that vendor's workarounds to a
   * renderer that is not it. Naming ourselves means no entry matches and the
   * engine's defaults stand -- which is the behaviour the --vk path already
   * relies on and states.
   */
  snprintf(id->Driver, sizeof id->Driver, "x2native");
  snprintf(id->Description, sizeof id->Description, "x2native host Direct3D 8");
  id->DriverVersionLow = 0;
  id->DriverVersionHigh = 8u << 16;
  id->WHQLLevel = 1;
  d3d8_ret(C, D3D_OK);
}

static void d3d8_GetAdapterModeCount(D3D8Object *self, CPU *C) {
  (void)self;
  d3d8_ret(C, (uint32_t)(nmodes() * NFORMATS));
}

static void d3d8_EnumAdapterModes(D3D8Object *self, CPU *C) {
  uint32_t mode = d3d8_arg(C, 1);
  D3DDISPLAYMODE *out = (D3DDISPLAYMODE *)guest_ptr(d3d8_arg(C, 2), "mode");
  int imode;
  (void)self;
  if (!out) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  if (mode >= (uint32_t)(nmodes() * NFORMATS)) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  imode = (int)(mode / NFORMATS);
  if (imode < NBASE_MODES) {
    out->Width = g_base_modes[imode].w;
    out->Height = g_base_modes[imode].h;
  } else {
    out->Width = g_published_mode.w;
    out->Height = g_published_mode.h;
  }
  out->RefreshRate = DESKTOP_HZ;
  out->Format = g_mode_formats[mode % NFORMATS];
  d3d8_ret(C, D3D_OK);
}

static void d3d8_GetAdapterDisplayMode(D3D8Object *self, CPU *C) {
  D3DDISPLAYMODE *out = (D3DDISPLAYMODE *)guest_ptr(d3d8_arg(C, 1), "mode");
  uint32_t w, h;
  (void)self;
  if (!out) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  current_desktop(&w, &h);
  out->Width = w;
  out->Height = h;
  out->RefreshRate = DESKTOP_HZ;
  out->Format = DESKTOP_FORMAT;
  d3d8_ret(C, D3D_OK);
}

/* ---- what the adapter supports ----------------------------------------- */

/*
 * The three Check* methods are the engine asking permission. Answering D3D_OK
 * to everything would be the bandaid: the engine would then pick a format the
 * backend cannot present and the failure would land in the swapchain. So each
 * answers from an explicit list, and a format that is not on it is refused
 * with the same code real D3D8 uses -- which the engine is written to handle,
 * because on Windows it gets that answer too.
 */
static int format_is_backbuffer(uint32_t f) {
  return f == D3DFMT_X8R8G8B8 || f == D3DFMT_A8R8G8B8 || f == D3DFMT_R5G6B5 ||
         f == D3DFMT_X1R5G5B5 || f == D3DFMT_A1R5G5B5;
}

static int format_is_depth(uint32_t f) {
  return f == D3DFMT_D16 || f == D3DFMT_D24S8 || f == D3DFMT_D24X8 ||
         f == D3DFMT_D32 || f == D3DFMT_D15S1;
}

static int format_is_texture(uint32_t f) {
  return format_is_backbuffer(f) || f == D3DFMT_A4R4G4B4 || f == D3DFMT_A8 ||
         f == D3DFMT_R8G8B8 || f == D3DFMT_DXT1 || f == D3DFMT_DXT2 ||
         f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

static void d3d8_CheckDeviceType(D3D8Object *self, CPU *C) {
  uint32_t adapter_fmt = d3d8_arg(C, 2), backbuf_fmt = d3d8_arg(C, 3);
  (void)self;
  d3d8_ret(C, (format_is_backbuffer(adapter_fmt) &&
               format_is_backbuffer(backbuf_fmt))
                  ? D3D_OK
                  : D3DERR_NOTAVAILABLE);
}

static void d3d8_CheckDeviceFormat(D3D8Object *self, CPU *C) {
  uint32_t check_fmt = d3d8_arg(C, 5);
  (void)self;
  d3d8_ret(C, (format_is_texture(check_fmt) || format_is_depth(check_fmt))
                  ? D3D_OK
                  : D3DERR_NOTAVAILABLE);
}

static void d3d8_CheckDeviceMultiSampleType(D3D8Object *self, CPU *C) {
  /* D3DMULTISAMPLE_NONE is 0. Nothing else is offered: the swapchain this
     backend claims is single-sampled, and saying otherwise would have the
     engine create render targets that cannot be resolved. */
  uint32_t type = d3d8_arg(C, 4);
  (void)self;
  d3d8_ret(C, type == 0 ? D3D_OK : D3DERR_NOTAVAILABLE);
}

static void d3d8_CheckDepthStencilMatch(D3D8Object *self, CPU *C) {
  uint32_t rt = d3d8_arg(C, 3), ds = d3d8_arg(C, 4);
  (void)self;
  d3d8_ret(C, (format_is_backbuffer(rt) && format_is_depth(ds))
                  ? D3D_OK
                  : D3DERR_NOTAVAILABLE);
}

static void d3d8_GetDeviceCaps(D3D8Object *self, CPU *C) {
  uint32_t adapter = d3d8_arg(C, 0), devtype = d3d8_arg(C, 1);
  D3DCAPS8 *caps = (D3DCAPS8 *)guest_ptr(d3d8_arg(C, 2), "caps block");
  (void)self;
  if (!caps) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  if (devtype != D3DDEVTYPE_HAL) {
    /* The reference rasteriser is not implemented, and pretending it is
       would have the engine fall back to it on any HAL failure and then
       render nothing, silently. */
    d3d8_ret(C, D3DERR_NOTAVAILABLE);
    return;
  }
  d3d8_caps_fill(caps, adapter, devtype, &g_d3d8.limits);
  { /* Once. The engine asks repeatedly and the block never changes. */
    static int told;
    if (!told++)
      d3d8_caps_dump(caps, "IDirect3D8::GetDeviceCaps");
  }
  d3d8_ret(C, D3D_OK);
}

static void d3d8_GetAdapterMonitor(D3D8Object *self, CPU *C) {
  /* An HMONITOR the guest only ever passes back to Win32, which this host
     also implements. One adapter, one token. */
  (void)self;
  d3d8_ret(C, 0x00110001u);
}

/* ---- CreateDevice ------------------------------------------------------ */

static void d3d8_CreateDevice(D3D8Object *self, CPU *C) {
  uint32_t adapter = d3d8_arg(C, 0);
  uint32_t devtype = d3d8_arg(C, 1);
  uint32_t hwnd = d3d8_arg(C, 2);
  uint32_t behaviour = d3d8_arg(C, 3);
  D3DPRESENT_PARAMETERS *pp =
      (D3DPRESENT_PARAMETERS *)guest_ptr(d3d8_arg(C, 4), "present parameters");
  uint32_t out = d3d8_arg(C, 5);
  D3D8Object *dev;

  (void)self;
  if (!pp || !out) {
    d3d8_ret(C, D3DERR_INVALIDCALL);
    return;
  }
  if (devtype != D3DDEVTYPE_HAL) {
    d3d8_ret(C, D3DERR_NOTAVAILABLE);
    return;
  }

  dev = d3d8_device_create(adapter, devtype, hwnd, behaviour, pp);
  if (!dev) {
    WR32(out, 0);
    d3d8_ret(C, D3DERR_NOTAVAILABLE);
    return;
  }
  WR32(out, d3d8_object_guest(dev));
  d3d8_ret(C, D3D_OK);
}

/* ---- installation ------------------------------------------------------ */

static const D3D8MethodFn g_impl[] = {d3d8_QueryInterface,
                                      d3d8_AddRef,
                                      d3d8_Release,
                                      NULL, /* RegisterSoftwareDevice */
                                      d3d8_GetAdapterCount,
                                      d3d8_GetAdapterIdentifier,
                                      d3d8_GetAdapterModeCount,
                                      d3d8_EnumAdapterModes,
                                      d3d8_GetAdapterDisplayMode,
                                      d3d8_CheckDeviceType,
                                      d3d8_CheckDeviceFormat,
                                      d3d8_CheckDeviceMultiSampleType,
                                      d3d8_CheckDepthStencilMatch,
                                      d3d8_GetDeviceCaps,
                                      d3d8_GetAdapterMonitor,
                                      d3d8_CreateDevice};

static int g_enabled;

int d3d8_host_enabled(void) { return g_enabled; }

/*
 * d3d8.dll!Direct3DCreate8(UINT SDKVersion) -- __stdcall, one argument.
 *
 * A STRONG definition of the symbol the generated dispatch table declares
 * weak, so linking this file is what arms it. Until d3d8_host_enable() has
 * been called it does exactly what the weak default did, which is what keeps
 * a build with this linked comparable to one without.
 */
void imp_d3d8_Direct3DCreate8(CPU *C);
void imp_d3d8_Direct3DCreate8(CPU *C) {
  uint32_t sdk = RD32(C->esp + 4u);

  if (!g_enabled) {
    /* Exactly what the weak default reaches: there is no d3d8.dll to
       forward to, so the honest answer is that the import is not
       implemented -- said by name, and loudly.
       It says which FLAG arms it, because the message without that reads
       as "this port has no renderer" and cost a session a rebuild and a
       bisection before the answer turned out to be a missing --d3d8. */
    fprintf(stderr, "d3d8: the host Direct3D 8 is LINKED but not ARMED. "
                    "This run asked for --run; the renderer is armed by "
                    "--d3d8 (see ./run.sh, which passes it).\n");
    x86_missing_import("d3d8.dll", "Direct3DCreate8");
    return;
  }
  if (sdk != D3D_SDK_VERSION_D3D8)
    fprintf(stderr,
            "d3d8: Direct3DCreate8(SDKVersion=%u); this host "
            "implements %d. Answering anyway -- the version only "
            "selects which d3d8.dll a real system would bind.\n",
            sdk, D3D_SDK_VERSION_D3D8);

  if (!g_d3d8_obj) {
    d3d8_caps_limits_default(&g_d3d8.limits);
    d3d8_iface_implement(D3D8_IF_IDirect3D8, g_impl,
                         (int)(sizeof g_impl / sizeof g_impl[0]));
    d3d8_device_install();
    d3d8_surface_install();
    d3d8_resource_install();
    g_d3d8_obj = d3d8_object_new(D3D8_IF_IDirect3D8, &g_d3d8);
    printf("d3d8: Direct3DCreate8 -> IDirect3D8 at 0x%08x\n",
           d3d8_object_guest(g_d3d8_obj));
    fflush(stdout);
  } else {
    /* Direct3DCreate8 hands out a NEW reference each time, exactly as the
       real one does; the engine will Release each. */
    d3d8_object_addref(g_d3d8_obj);
  }
  C->eax = d3d8_object_guest(g_d3d8_obj);
  C->esp += 4u + 4u; /* __stdcall, one argument */
}

/*
 * The IDirect3D8 this host handed out, for the one caller that legitimately
 * needs it back: IDirect3DDevice8::GetDirect3D.
 *
 * `ensure` creates it WITHOUT going through the import, so the self-test can
 * exercise GetDirect3D with no guest and no CPU state. It deliberately does
 * not touch the reference count -- Direct3DCreate8 owns that side.
 */
uint32_t d3d8_the_direct3d8(void) {
  return g_d3d8_obj ? d3d8_object_guest(g_d3d8_obj) : 0;
}

unsigned d3d8_the_direct3d8_refs(void) {
  return g_d3d8_obj ? (unsigned)d3d8_object_refs(g_d3d8_obj) : 0u;
}

void d3d8_the_direct3d8_ensure(void) {
  if (g_d3d8_obj)
    return;
  d3d8_caps_limits_default(&g_d3d8.limits);
  d3d8_iface_implement(D3D8_IF_IDirect3D8, g_impl,
                       (int)(sizeof g_impl / sizeof g_impl[0]));
  g_d3d8_obj = d3d8_object_new(D3D8_IF_IDirect3D8, &g_d3d8);
}

int d3d8_the_direct3d8_addref(void) {
  if (!g_d3d8_obj)
    return 0;
  d3d8_object_addref(g_d3d8_obj);
  return 1;
}

void d3d8_host_enable(void) { g_enabled = 1; }
