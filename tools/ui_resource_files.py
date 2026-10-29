"""The UI resource files every desktop package ships beside the binary.

The build stages them under build/<tree>/ui; Android and the web package copy
that whole directory, and the AppImage and macOS packagers copy exactly these.
"""

UI_FILES = (
    "LatoLatin-Regular.ttf",
    "LatoLatin-Bold.ttf",
    # packaging/xmen2-port.svg, for the windows' own icon.
    "app_icon.svg",
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
    # Port Settings' device-assignment marks (assets/ui/assign_*.svg).
    "assign_on.svg",
    "assign_off.svg",
)
