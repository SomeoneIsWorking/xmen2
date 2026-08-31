#!/usr/bin/env python3
"""Fail-closed checks for the Android entry point and signing contract."""

from pathlib import Path
import tempfile

from tools import build_android


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    raw = ROOT / "scratch/raw"
    raw.mkdir(parents=True, exist_ok=True)
    assert build_android.parse_java_major('openjdk version "25.0.4" 2026-08-18') == 25
    assert build_android.parse_java_major('openjdk version "26.0.2" 2026-07-21') == 26
    assert build_android.parse_java_major('java version "1.8.0_412"') == 8
    assert build_android.parse_java_major('java version "1"') is None
    assert build_android.parse_java_major("unrecognized runtime") is None
    assert build_android.parse_javac_major("javac 26.0.2") == 26
    assert build_android.parse_javac_major("unrecognized compiler") is None
    assert build_android.DEFAULT_ANDROID_API == 21
    assert build_android.native_jobs({}) == 2
    assert build_android.native_jobs({"X2_ANDROID_NATIVE_JOBS": "6"}) == 6
    assert build_android.native_prefix(Path("build"), 33, "x86_64") == (
        Path("build/deps/android/android-33/x86_64"))
    for invalid in ("0", "-1", "many"):
        try:
            build_android.native_jobs({"X2_ANDROID_NATIVE_JOBS": invalid})
        except SystemExit as error:
            assert "positive integer" in str(error)
        else:
            raise AssertionError(f"invalid Android native job count accepted: {invalid}")

    with tempfile.TemporaryDirectory(prefix="android-generator-test-", dir=raw) as directory:
        root = Path(directory) / "build"
        build = root / "android-x86_64"
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n", encoding="utf-8")
        stale = build / "stale"
        stale.write_text("generated", encoding="utf-8")
        build_android.prepare_native_build_directory(build, root)
        assert build.is_dir()
        assert not stale.exists()
        assert build_android.cached_generator(build) is None

        (build / "CMakeCache.txt").write_text(
            "CMAKE_GENERATOR:INTERNAL=Ninja\n", encoding="utf-8")
        retained = build / "retained"
        retained.write_text("generated", encoding="utf-8")
        build_android.prepare_native_build_directory(build, root)
        assert retained.is_file()

        outside = Path(directory) / "outside"
        outside.mkdir()
        try:
            build_android.prepare_native_build_directory(outside, root)
        except SystemExit as error:
            assert "outside direct build/ child" in str(error)
        else:
            raise AssertionError("Android build migration accepted an external path")

    try:
        build_android.release_signing({})
    except SystemExit as error:
        assert "unsigned release APK" in str(error)
    else:
        raise AssertionError("missing signing inputs were accepted")

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
    assert "discardValidatedDocument" in setup
    assert "discardRejectedImport" in setup
    assert setup.count("discardRejectedImport(result)") == 2
    assert "pickTree" in setup
    assert "pickDocument" in setup
    assert "promoteValidated" in setup
    assert "nativeValidateInstall" in setup
    # Trace watches are diagnostic-only and are intentionally exposed only by
    # a debug build. The JNI bridge accepts the recompiler watch grammar, not
    # arbitrary environment-variable names or values.
    assert "BuildConfig.DEBUG" in setup
    assert "com.someoneisworking.xmen2.trace.arguments" in setup
    assert "com.someoneisworking.xmen2.trace.cap" in setup
    bridge = (ROOT / "src/native/android_bridge.cpp").read_text(encoding="utf-8")
    native_runner = (ROOT / "src/native/x2native.c").read_text(encoding="utf-8")
    assert "valid_trace_watch" in bridge
    assert '::setenv("X2_ARGS", arguments, 1)' in bridge
    assert "generic setenv bridge" in bridge
    assert "buildConfig = true" in gradle
    assert "#if !defined(__ANDROID__)\n#include <execinfo.h>" in native_runner
    assert "[HOST STACK] unavailable on Android" in native_runner
    assert "::dup2(pipe_descriptors[1], STDOUT_FILENO)" in bridge
    assert "::dup2(pipe_descriptors[1], STDERR_FILENO)" in bridge
    runtime = (ROOT / "src/native/x86rt_native.c").read_text(encoding="utf-8")
    assert "syscall(SYS_process_vm_readv" in runtime
    draw_trace = (ROOT / "src/gpu/gpu_draw_trace.c").read_text(encoding="utf-8")
    assert "funopen(&g_capture" in draw_trace
    assert "capture_close(capture, &capture_text, &capture_size)" in draw_trace
    assert "MANAGE_EXTERNAL_STORAGE" not in manifest
    assert not (ROOT / "android/app/src/main/java/com/someoneisworking/xmen2/"
                "InstallLocation.java").exists()
    assert "8e126b2" in cmake
    assert "x2.lucentJavaDir" in cmake
    assert "lucentJavaDir" in gradle
    # The product target always opens the control channel, and socket() needs
    # this permission's inet group; without it control_start() exit(2)s before
    # the game runs, which presented as an unexplained crash on device.
    assert "android.permission.INTERNET" in manifest
    for unsafe in ("getExternalStorageDirectory", "/storage",
                   "ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION"):
        assert unsafe not in setup, f"{unsafe} would bypass the scoped picker"

    # The common Android prefix owns FFmpeg's source/archive mechanics as well
    # as SDL. X-Men only resolves and consumes that one shared build contract.
    # A prefix is also API-specific: one Android API build must never replace
    # another API build's libraries with a different compatibility floor.
    android_port = ROOT / "vendor/shared/android-port"
    original_shared_dir = build_android.shared_dir
    build_android.shared_dir = lambda name, marker: str(android_port)
    try:
        assert build_android.android_port_tool() == android_port / "tools/android_port.py"
    finally:
        build_android.shared_dir = original_shared_dir
    assert "build_android_deps.py" not in cmake
    assert "X2_ANDROID_PORT_PREFIX" in cmake
    assert not (ROOT / "tools/build_android_deps.py").exists()
    assert '"-DX2_NATIVE_TRACE=OFF"' in (ROOT / "tools/build_android.py").read_text(
        encoding="utf-8")

    assert "extends LucentActivity" in activity
    assert "WindowInsetsController" not in activity
    assert 'setText("Browse for XMen2.exe")' in setup
    assert "folder containing XMen2.exe" in setup
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
