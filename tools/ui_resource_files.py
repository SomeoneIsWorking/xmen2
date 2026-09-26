"""The UI resource files every desktop package ships beside the binary.

The build stages them under build/<tree>/ui; Android and the web package copy
that whole directory, and the AppImage and macOS packagers copy exactly these.
"""

UI_FILES = (
    "LatoLatin-Regular.ttf",
    "LatoLatin-Bold.ttf",
    # The shared key typeface keyboard prompts are lettered in at runtime.
    "NotoSans-Bold-keys.ttf",
    "settings.rcss",
    "touch_controls.rcss",
    # The touch buttons' authored art (assets/ui/touch_*.svg).
    "touch_punch.svg",
    "touch_smash.svg",
    "touch_use.svg",
    "touch_jump.svg",
    "touch_menu.svg",
    # The menu pad's buttons: shared port-assets Xbox glyphs, staged by the
    # build under these names.
    "pad_dpad_up.svg",
    "pad_dpad_down.svg",
    "pad_dpad_left.svg",
    "pad_dpad_right.svg",
    "pad_face_a.svg",
    "pad_face_b.svg",
    "pad_face_x.svg",
    "pad_face_y.svg",
    "pad_lb.svg",
    "pad_rb.svg",
)
