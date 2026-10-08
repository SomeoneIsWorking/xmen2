#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

static void check_hud_configuration(const char *path) {
  static const char *const invalid_lines[] = {"layout=unknown",
                                              "vitals_scale_percent=49",
                                              "vitals_scale_percent=151",
                                              "potions_scale_percent=-1",
                                              "potions_scale_percent=nan",
                                              "portraits_scale_percent=inf",
                                              "portraits_scale_percent=100.5",
                                              "safe_inset_percent=11",
                                              "safe_inset_percent=-1",
                                              "safe_inset_percent=1e0",
                                              "unknown=100"};
  X2Settings settings, original, loaded;
  char why[256];
  x2_settings_defaults(&original);
  original.hud = (X2HudSettings){X2_HUD_LAYOUT_MOBILE, 50, 150, 125, 10};
  CHECK(x2_settings_save(&original, path, why, sizeof why));
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&original, &loaded, sizeof original) == 0);

  /* Saving an invalid edit preserves the previous persistent selection. */
  settings = original;
  settings.hud.layout = (X2HudLayout)-1;
  CHECK(!x2_settings_save(&settings, path, why, sizeof why));
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&original, &loaded, sizeof original) == 0);
  CHECK(strcmp(x2_hud_layout_name(settings.hud.layout), "invalid") == 0);
  settings.hud = original.hud;
  settings.hud.safe_inset_percent = 0;
  CHECK(x2_hud_settings_valid(&settings.hud));

  for (unsigned i = 0; i < sizeof invalid_lines / sizeof invalid_lines[0];
       i++) {
    FILE *file = fopen(path, "w");
    assert(file);
    CHECK(fprintf(file, "ui.hud.layout=retail\nui.hud.%s\n", invalid_lines[i]) >
          0);
    CHECK(fclose(file) == 0);
    loaded = original;
    CHECK(!x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(strstr(why, ":2") != NULL);
    CHECK(memcmp(&original, &loaded, sizeof original) == 0);
  }

  /* The standalone parser is transactional too, even after a valid edit. */
  CHECK(x2_hud_settings_parse(&settings.hud, "layout", "retail"));
  original.hud = settings.hud;
  CHECK(!x2_hud_settings_parse(&settings.hud, "vitals_scale_percent", "nan"));
  CHECK(memcmp(&original.hud, &settings.hud, sizeof settings.hud) == 0);
}

/* Changing one controller's seat never turns another controller off. */
static void check_controller_change_keeps_other_controller(void) {
  X2Settings settings;
  x2_settings_defaults(&settings);
  CHECK(x2_settings_assign_controller(&settings, "pad-a", 0));
  CHECK(x2_settings_assign_controller(&settings, "pad-b", 1));

  CHECK(x2_settings_assign_controller(&settings, "pad-a", 2));
  CHECK(x2_settings_controller_player(&settings, "pad-a") == 2);
  CHECK(x2_settings_controller_player(&settings, "pad-b") == 1);

  CHECK(x2_settings_assign_controller(&settings, "pad-a", 1));
  CHECK(x2_settings_controller_player(&settings, "pad-a") == 1);
  CHECK(x2_settings_controller_player(&settings, "pad-b") == 2);

  CHECK(x2_settings_assign_controller(&settings, "pad-b", 0));
  CHECK(x2_settings_controller_player(&settings, "pad-b") == 0);
  CHECK(x2_settings_controller_player(&settings, "pad-a") == 1);

  CHECK(x2_settings_assign_controller(&settings, "pad-a",
                                      X2_SETTINGS_UNASSIGNED));
  CHECK(x2_settings_controller_player(&settings, "pad-b") == 0);
}

/* A device displaced across kinds takes the seat the mover left when that
   seat can hold it, and only otherwise goes off. */
static void check_displaced_device_takes_vacated_seat(void) {
  X2Settings settings;
  x2_settings_defaults(&settings);
  CHECK(x2_settings_assign_keyboard(&settings, 1, 1));
  CHECK(x2_settings_assign_controller(&settings, "pad-a", 2));

  CHECK(x2_settings_assign_controller(&settings, "pad-a", 1));
  CHECK(x2_settings_player_keyboard(&settings, 2) == 1);
  CHECK(x2_settings_controller_player(&settings, "pad-a") == 1);

  /* The only P1 keyboard can move: the displaced keyboard takes P1. */
  CHECK(x2_settings_assign_keyboard(&settings, 0, 2));
  CHECK(x2_settings_player_keyboard(&settings, 0) == 1);
  CHECK(x2_settings_player_keyboard(&settings, 2) == 0);

  /* P1 already holds a keyboard, so a controller-displaced keyboard cannot
     return there and goes off. */
  CHECK(x2_settings_assign_controller(&settings, "pad-b", 0));
  CHECK(x2_settings_assign_controller(&settings, "pad-b", 2));
  CHECK(x2_settings_player_keyboard(&settings, 0) == 1);
  CHECK(x2_settings_player_keyboard(&settings, 2) == -1);
  CHECK(settings.keyboard_player[0] == X2_SETTINGS_UNASSIGNED);
  CHECK(x2_settings_controller_player(&settings, "pad-a") == 1);
}

static void check_keyboard_profile_restore(const char *path) {
  X2Settings settings, loaded;
  char why[256];
  x2_settings_defaults(&settings);
  settings.keyboard_profile[1].keyboard_set[4] = 1;
  settings.keyboard_profile[1].keyboard[4] = 30;
  settings.keyboard_profile[1].keyboard_set[5] = 1;
  settings.keyboard_profile[1].keyboard[5] = 0;
  settings.keyboard_profile[2].keyboard_set[6] = 1;
  settings.keyboard_profile[2].keyboard[6] = 31;

  x2_keyboard_profile_restore_row(&settings.keyboard_profile[1], 4);
  CHECK(!settings.keyboard_profile[1].keyboard_set[4]);
  CHECK(settings.keyboard_profile[1].keyboard_set[5]);
  CHECK(x2_settings_save(&settings, path, why, sizeof why));
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&settings, &loaded, sizeof settings) == 0);

  x2_keyboard_profile_restore_all(&settings.keyboard_profile[1]);
  CHECK(!settings.keyboard_profile[1].keyboard_set[5]);
  CHECK(settings.keyboard_profile[2].keyboard_set[6]);
  CHECK(x2_settings_save(&settings, path, why, sizeof why));
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&settings, &loaded, sizeof settings) == 0);
}

static void check_extraction_revive(const char *path) {
  X2Settings saved, loaded, untouched;
  char why[256];
  FILE *file;
  x2_settings_defaults(&saved);
  CHECK(saved.extraction_revive == X2_EXTRACTION_REVIVE_OFF);
  for (int mode = X2_EXTRACTION_REVIVE_OFF; mode <= X2_EXTRACTION_REVIVE_PAID;
       mode++) {
    saved.extraction_revive = (X2ExtractionRevive)mode;
    CHECK(x2_settings_save(&saved, path, why, sizeof why));
    CHECK(x2_settings_load(&loaded, path, why, sizeof why));
    CHECK(loaded.extraction_revive == (X2ExtractionRevive)mode);
    X2ExtractionRevive parsed;
    CHECK(x2_extraction_revive_parse(
        x2_extraction_revive_name((X2ExtractionRevive)mode), &parsed));
    CHECK(parsed == (X2ExtractionRevive)mode);
  }
  file = fopen(path, "r");
  assert(file);
  char text[4096] = {0};
  size_t used = fread(text, 1, sizeof text - 1, file);
  fclose(file);
  CHECK(used > 0 && strstr(text, "gameplay.extraction_revive=paid\n"));
  /* A file written before the setting existed keeps the retail rule. */
  file = fopen(path, "w");
  assert(file);
  fprintf(file, "video.width=1280\nvideo.height=720\nvideo.mode=windowed\n");
  fclose(file);
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(loaded.extraction_revive == X2_EXTRACTION_REVIVE_OFF);
  /* An unknown value refuses and leaves the settings alone. */
  untouched = loaded;
  file = fopen(path, "w");
  assert(file);
  fprintf(file, "gameplay.extraction_revive=sometimes\n");
  fclose(file);
  CHECK(!x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&loaded, &untouched, sizeof loaded) == 0);
  CHECK(strstr(why, ":1") != NULL);
}

int main(void) {
  const char *path = X2_TEST_SETTINGS_PATH;
  X2Settings saved, loaded, untouched;
  char why[256];
  FILE *file;

  remove(path);
  x2_settings_defaults(&saved);
  CHECK(saved.width == 1280 && saved.height == 720);
  CHECK(saved.window_mode == X2_WINDOW_WINDOWED);
  CHECK(saved.dynamic_shadows == 1 && saved.shadow_resolution == 2048);
  CHECK(saved.touch_controls == X2_TOUCH_CONTROLS_AUTO);
  CHECK(strcmp(x2_touch_controls_label(saved.touch_controls), "Automatic") ==
        0);
  CHECK(strcmp(x2_touch_controls_label(X2_TOUCH_CONTROLS_OFF), "Off") == 0);
  CHECK(strcmp(x2_touch_controls_label(X2_TOUCH_CONTROLS_ALWAYS), "Always") ==
        0);
  CHECK(saved.hud.layout == X2_HUD_LAYOUT_AUTO);
  CHECK(saved.hud.vitals_scale_percent == 100);
  CHECK(saved.hud.potions_scale_percent == 100);
  CHECK(saved.hud.portraits_scale_percent == 100);
  CHECK(saved.hud.safe_inset_percent == 2);
  CHECK(saved.boot_mode == x2::config::BootMode::Normal);
  CHECK(strcmp(x2::config::boot_mode_label(saved.boot_mode), "Boot normally") ==
        0);
  CHECK(x2::config::boot_mode_parse("menu", &saved.boot_mode));
  CHECK(saved.boot_mode == x2::config::BootMode::Menu);
  CHECK(!x2::config::boot_mode_parse("new-game", &saved.boot_mode));
  CHECK(x2_settings_player_keyboard(&saved, 0) == 0);
  CHECK(x2_settings_player_keyboard(&saved, 1) == -1);

  saved.width = 1920;
  saved.height = 1080;
  saved.window_mode = X2_WINDOW_BORDERLESS;
  saved.dynamic_shadows = 0;
  saved.shadow_resolution = 4096;
  saved.touch_controls = X2_TOUCH_CONTROLS_ALWAYS;
  saved.boot_mode = x2::config::BootMode::Continue;
  CHECK(x2_settings_assign_keyboard(&saved, 2, 0));
  CHECK(x2_settings_player_keyboard(&saved, 0) == 2);
  CHECK(saved.keyboard_player[0] == X2_SETTINGS_UNASSIGNED);
  CHECK(x2_settings_assign_controller(&saved, "sdl-045e-028e-a1", 0));
  CHECK(strcmp(x2_settings_player_controller(&saved, 0), "sdl-045e-028e-a1") ==
        0);
  saved.keyboard_profile[2].keyboard_set[4] = 1;
  saved.keyboard_profile[2].keyboard[4] = 30;
  CHECK(x2_settings_save(&saved, path, why, sizeof why));
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&saved, &loaded, sizeof saved) == 0);

  /* A device has one owner. P2-P4 have exactly one device kind when
     assigned, so changing kind evicts the other kind. */
  CHECK(x2_settings_assign_keyboard(&loaded, 1, 2));
  CHECK(x2_settings_assign_keyboard(&loaded, 3, 2));
  CHECK(loaded.keyboard_player[1] == X2_SETTINGS_UNASSIGNED);
  CHECK(x2_settings_assign_controller(&loaded, "pad-b", 2));
  CHECK(x2_settings_assign_controller(&loaded, "pad-c", 2));
  CHECK(x2_settings_player_keyboard(&loaded, 2) == -1);
  CHECK(x2_settings_controller_player(&loaded, "pad-b") ==
        X2_SETTINGS_UNASSIGNED);
  CHECK(x2_settings_controller_player(&loaded, "pad-c") == 2);

  /* Only P1 can hotswap. A controller assignment replaces P2's keyboard,
     while P1 retains its keyboard and controller together. */
  x2_settings_defaults(&loaded);
  CHECK(x2_settings_assign_keyboard(&loaded, 1, 1));
  CHECK(x2_settings_assign_controller(&loaded, "hot-pad-a", 0));
  CHECK(x2_settings_assign_controller(&loaded, "hot-pad-b", 1));
  CHECK(x2_settings_player_keyboard(&loaded, 0) == 0);
  CHECK(x2_settings_player_keyboard(&loaded, 1) == -1);
  CHECK(strcmp(x2_settings_player_controller(&loaded, 0), "hot-pad-a") == 0);
  CHECK(strcmp(x2_settings_player_controller(&loaded, 1), "hot-pad-b") == 0);

  /* P1 is the retail primary player and may not be left with no device. */
  CHECK(x2_settings_assign_controller(&loaded, "hot-pad-a",
                                      X2_SETTINGS_UNASSIGNED));
  CHECK(!x2_settings_assign_keyboard(&loaded, 0, X2_SETTINGS_UNASSIGNED));
  CHECK(x2_settings_player_keyboard(&loaded, 0) == 0);

  /* Old Auto migrates to its keyboard profile only; an old explicit pad is
     reserved by identity and never becomes roaming controller policy. */
  file = fopen(path, "w");
  assert(file);
  fprintf(file, "video.width=1280\nvideo.height=720\nvideo.mode=windowed\n"
                "input.player0.device=auto\ninput.player0.profile=1\n"
                "input.player1.device=gamepad:legacy-pad\n"
                "input.player1.profile=0\n"
                "input.player2.device=none\ninput.player2.profile=2\n"
                "input.player3.device=none\ninput.player3.profile=3\n");
  fclose(file);
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(x2_settings_player_keyboard(&loaded, 0) == 1);
  CHECK(strcmp(x2_settings_player_controller(&loaded, 1), "legacy-pad") == 0);
  CHECK(x2_settings_player_keyboard(&loaded, 1) == -1);

  /* Legacy allowed multiple players to reference one profile. The grid does
     not: later owners receive the lowest unreserved row with bindings cloned.
   */
  file = fopen(path, "w");
  assert(file);
  fprintf(file, "video.width=1280\nvideo.height=720\nvideo.mode=windowed\n"
                "input.player0.device=keyboard\ninput.player0.profile=1\n"
                "input.player1.device=auto\ninput.player1.profile=1\n"
                "input.player2.device=none\ninput.player2.profile=2\n"
                "input.player3.device=none\ninput.player3.profile=3\n"
                "input.profile1.row4=77\n");
  fclose(file);
  CHECK(x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(x2_settings_player_keyboard(&loaded, 0) == 1);
  CHECK(x2_settings_player_keyboard(&loaded, 1) == 0);
  CHECK(loaded.keyboard_profile[0].keyboard_set[4] == 1);
  CHECK(loaded.keyboard_profile[0].keyboard[4] == 77);
  /* Existing files have no HUD keys and preserve the prior automatic layout. */
  CHECK(loaded.hud.layout == X2_HUD_LAYOUT_AUTO);
  CHECK(loaded.hud.vitals_scale_percent == 100);
  CHECK(loaded.hud.potions_scale_percent == 100);
  CHECK(loaded.hud.portraits_scale_percent == 100);
  CHECK(loaded.hud.safe_inset_percent == 2);

  untouched = loaded;
  file = fopen(path, "w");
  assert(file);
  fprintf(file, "video.width=12\n");
  fclose(file);
  CHECK(!x2_settings_load(&loaded, path, why, sizeof why));
  CHECK(memcmp(&loaded, &untouched, sizeof loaded) == 0);
  CHECK(strstr(why, ":1") != NULL);
  check_hud_configuration(path);
  check_extraction_revive(path);
  check_controller_change_keeps_other_controller();
  check_displaced_device_takes_vacated_seat();
  check_keyboard_profile_restore(path);
  remove(path);

  printf("test_settings: %d checks passed\n", checks);
  return 0;
}
