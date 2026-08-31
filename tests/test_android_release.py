#!/usr/bin/env python3
"""Fail-closed checks for the Android entry point and signing contract."""

from pathlib import Path
import tarfile
import tempfile

from tools import build_android, build_android_deps


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    assert build_android.parse_java_major('openjdk version "25.0.4" 2026-08-18') == 25
    assert build_android.parse_java_major('openjdk version "26.0.2" 2026-07-21') == 26
    assert build_android.parse_java_major('java version "1.8.0_412"') == 8
    assert build_android.parse_java_major('java version "1"') is None
    assert build_android.parse_java_major("unrecognized runtime") is None
    assert build_android.parse_javac_major("javac 26.0.2") == 26
    assert build_android.parse_javac_major("unrecognized compiler") is None

    try:
        build_android.release_signing({})
    except SystemExit as error:
        assert "unsigned release APK" in str(error)
    else:
        raise AssertionError("missing signing inputs were accepted")

    raw = ROOT / "scratch/raw"
    raw.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="android-release-test-", dir=raw) as directory:
        key = Path(directory) / "release.jks"
        key.write_bytes(b"fixture")
        signing = build_android.release_signing({
            "X2_ANDROID_KEYSTORE": str(key),
            "X2_ANDROID_KEY_ALIAS": "xmen2",
            "X2_ANDROID_STORE_PASSWORD": "store-fixture",
            "X2_ANDROID_KEY_PASSWORD": "key-fixture",
        })
        assert signing["X2_ANDROID_KEYSTORE"] == str(key.resolve())

    # The publish gate is what refuses an unsigned release. Gradle configures
    # without release keys so a debug device build is possible, so this refusal
    # must hold on its own rather than relying on a configuration-time throw.
    with tempfile.TemporaryDirectory(prefix="android-publish-test-", dir=raw) as directory:
        fake_root = Path(directory)
        outputs = fake_root / "android/app/build/outputs/apk/release"
        outputs.mkdir(parents=True)
        (outputs / "app-arm64-v8a-release-unsigned.apk").write_bytes(b"fixture")
        try:
            build_android.publish_apk(fake_root, "arm64-v8a")
        except SystemExit as error:
            assert "Expected exactly one signed release APK" in str(error)
        else:
            raise AssertionError("an unsigned release APK was published")

        debug_outputs = fake_root / "android/app/build/outputs/apk/debug"
        debug_outputs.mkdir(parents=True)
        try:
            build_android.debug_apk(fake_root)
        except SystemExit as error:
            assert "Expected exactly one debug APK" in str(error)
        else:
            raise AssertionError("a missing debug APK was accepted")
        debug = debug_outputs / "app-arm64-v8a-debug.apk"
        debug.write_bytes(b"fixture")
        # A debug artifact stays in Gradle's output; it is never a release.
        assert build_android.debug_apk(fake_root) == debug
        assert not (fake_root / "build/release").exists()

    activity = (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "XMen2GameActivity.java").read_text(encoding="utf-8")
    manifest = (ROOT / "android/app/src/main/AndroidManifest.xml").read_text(
        encoding="utf-8")
    gradle = (ROOT / "android/app/build.gradle").read_text(encoding="utf-8")
    root_gradle = (ROOT / "android/build.gradle").read_text(encoding="utf-8")
    wrapper = (ROOT / "android/gradle/wrapper/gradle-wrapper.properties").read_text(
        encoding="utf-8")
    setup = (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
             "XMen2SetupActivity.java").read_text(encoding="utf-8")
    location = (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "InstallLocation.java").read_text(encoding="utf-8")
    # Browse opens the folder picker directly. A single-document URI grants
    # access to one file, so an XMen2.exe selection can never yield an install;
    # the setup must not ask for the executable and then ask again for a folder.
    assert "ACTION_OPEN_DOCUMENT_TREE" in setup
    assert 'endsWith(".exe")' not in setup
    # The install is read in place. Copying it through a content provider cost
    # 0.05 MB/s across this game's many small files, so no staging copy may
    # come back: no provider streaming, and no per-document tree walk.
    assert "MANAGE_EXTERNAL_STORAGE" in manifest
    assert "isExternalStorageManager" in setup
    # The product target always opens the control channel, and socket() needs
    # this permission's inet group; without it control_start() exit(2)s before
    # the game runs, which presented as an unexplained crash on device.
    assert "android.permission.INTERNET" in manifest
    for copying in ("openInputStream", "buildChildDocumentsUriUsingTree",
                    "buildDocumentUriUsingTree"):
        assert copying not in setup, f"{copying} would reintroduce the staging copy"
    assert not (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "InstallStaging.java").exists(), "the staging copy must not return"
    # A provider without a filesystem path is refused by name, never staged.
    assert "return null" in location
    assert 'relative.contains("..")' in location

    # The FFmpeg tree's directory name comes from the archive. Composing it
    # from the version spelled it "ffmpeg-n7.1.1" while GitHub's tarball roots
    # at "FFmpeg-n7.1.1", so it never matched: every build re-extracted the
    # 15 MB archive and then refused it as missing.
    members = [tarfile.TarInfo("FFmpeg-n7.1.1"),
               tarfile.TarInfo("FFmpeg-n7.1.1/configure")]
    assert build_android_deps.archive_root(members) == "FFmpeg-n7.1.1"
    try:
        build_android_deps.archive_root([*members, tarfile.TarInfo("elsewhere/x")])
    except SystemExit as error:
        assert "exactly one top-level directory" in str(error)
    else:
        raise AssertionError("an archive with two roots was accepted")

    assert "protected String getMainFunction()" in activity
    assert 'return "main";' in activity
    assert 'android:icon="@drawable/xmen2_port_icon"' in manifest
    assert "signingConfig = signingConfigs.release" in gradle
    assert "enableV3Signing = true" in gradle
    assert 'version "9.2.1"' in root_gradle
    assert "gradle-9.4.1-bin.zip" in wrapper
    assert "distributionSha256Sum=" in wrapper
    print("android release: toolchain, entry point, icon, and signing passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
