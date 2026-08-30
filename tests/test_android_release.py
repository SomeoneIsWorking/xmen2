#!/usr/bin/env python3
"""Fail-closed checks for the Android entry point and signing contract."""

from pathlib import Path
import tempfile

from tools import build_android


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

    activity = (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "XMen2GameActivity.java").read_text(encoding="utf-8")
    manifest = (ROOT / "android/app/src/main/AndroidManifest.xml").read_text(
        encoding="utf-8")
    gradle = (ROOT / "android/app/build.gradle").read_text(encoding="utf-8")
    root_gradle = (ROOT / "android/build.gradle").read_text(encoding="utf-8")
    wrapper = (ROOT / "android/gradle/wrapper/gradle-wrapper.properties").read_text(
        encoding="utf-8")
    assert "protected String getMainFunction()" in activity
    assert 'return "main";' in activity
    assert 'android:icon="@drawable/xmen2_port_icon"' in manifest
    assert "signingConfig = signingConfigs.release" in gradle
    assert "enableV3Signing = true" in gradle
    assert "Refusing to" in gradle and "unsigned release APK." in gradle
    assert 'version "9.2.1"' in root_gradle
    assert "gradle-9.4.1-bin.zip" in wrapper
    assert "distributionSha256Sum=" in wrapper
    print("android release: toolchain, entry point, icon, and signing passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
