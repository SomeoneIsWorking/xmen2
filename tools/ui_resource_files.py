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
)
