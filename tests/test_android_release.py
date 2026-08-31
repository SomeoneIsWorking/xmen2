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
    assert build_android.native_jobs({}) == 2
    assert build_android.native_jobs({"X2_ANDROID_NATIVE_JOBS": "6"}) == 6
    for invalid in ("0", "-1", "many"):
        try:
            build_android.native_jobs({"X2_ANDROID_NATIVE_JOBS": invalid})
        except SystemExit as error:
            assert "positive integer" in str(error)
        else:
            raise AssertionError(f"invalid Android native job count accepted: {invalid}")

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
            build_android.debug_apk(fake_root, "arm64-v8a")
        except SystemExit as error:
            assert "Expected exactly one arm64-v8a debug APK" in str(error)
        else:
            raise AssertionError("a missing debug APK was accepted")
        debug = debug_outputs / "app-arm64-v8a-debug.apk"
        debug.write_bytes(b"fixture")
        # A debug artifact stays in Gradle's output; it is never a release.
        assert build_android.debug_apk(fake_root, "arm64-v8a") == debug
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
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    # The setup uses Android's scoped picker: Lucent owns persisted SAF grants,
    # bounded app-private staging, cancellation, and promotion after title
    # validation. No port code may reconstruct a provider filesystem path or
    # request broad all-files access.
    assert "LucentDocumentImport" in setup
    assert "pickTree" in setup
    assert "pickDocument" in setup
    assert "promoteValidated" in setup
    assert "nativeValidateInstall" in setup
    assert "MANAGE_EXTERNAL_STORAGE" not in manifest
    assert not (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "InstallLocation.java").exists()
    assert "70a61b2" in cmake
    assert "x2.lucentJavaDir" in cmake
    assert "lucentJavaDir" in gradle
    # The product target always opens the control channel, and socket() needs
    # this permission's inet group; without it control_start() exit(2)s before
    # the game runs, which presented as an unexplained crash on device.
    assert "android.permission.INTERNET" in manifest
    for unsafe in ("getExternalStorageDirectory", "/storage",
                   "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION"):
        assert unsafe not in setup, f"{unsafe} would bypass the scoped picker"

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
    assert build_android_deps.assembly_configuration("arm64-v8a") == ()
    assert build_android_deps.assembly_configuration("x86_64") == (
        "--disable-x86asm", "--disable-inline-asm")
    assert "--disable-inline-asm" in build_android_deps.build_contract("x86_64", 34)
    assert build_android_deps.default_prefix("x86_64") == Path("build/deps/android/x86_64")

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
