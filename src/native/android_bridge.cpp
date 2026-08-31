#include "android_bridge.h"
#include "install_picker.h"

#include <cstring>
#include <cstdlib>

#if defined(__ANDROID__)
#include <jni.h>

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdio>

#include <lucent/platform_c.h>

namespace {

char install_source[4096];

bool copy_path(char *destination, const char *source)
{
    if (!source || !source[0]) {
        destination[0] = 0;
        return true;
    }
    const size_t length = std::strlen(source);
    if (length >= 4096) return false;
    std::memcpy(destination, source, length + 1);
    return true;
}

bool read_string(JNIEnv *environment, jstring value, char *destination)
{
    if (!value) return copy_path(destination, nullptr);
    const char *utf = environment->GetStringUTFChars(value, nullptr);
    if (!utf) return false;
    const bool copied = copy_path(destination, utf);
    environment->ReleaseStringUTFChars(value, utf);
    return copied;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_someoneisworking_xmen2_XMen2SetupActivity_nativeConfigureStorage(
    JNIEnv *environment, jclass, jstring data_directory, jstring source)
{
    char source_path[sizeof install_source];
    if (!read_string(environment, data_directory, source_path)) return JNI_FALSE;
    if (!lucent_platform_set_user_data_directory(source_path)) return JNI_FALSE;
    if (!read_string(environment, source, install_source)) return JNI_FALSE;
    /* The existing guest input path already knows how to create and map the
     * SDL virtual pad. Android supplies touch actions to that same pad, so
     * there is one binding path instead of a second mobile-only input stack. */
    return ::setenv("X2_VIRTUAL_PAD", "1", 1) == 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_someoneisworking_xmen2_XMen2SetupActivity_nativeValidateInstall(
    JNIEnv *environment, jclass, jstring source, jstring archive_destination)
{
    char source_path[sizeof install_source];
    char destination[sizeof install_source];
    char reason[512];
    if (!read_string(environment, source, source_path) ||
        !read_string(environment, archive_destination, destination)) return JNI_FALSE;
    return x2_install_picker_prepare_selection(
               source_path, destination[0] ? destination : nullptr,
               reason, sizeof reason)
               ? JNI_TRUE : JNI_FALSE;
}

namespace {

/*
 * Writes straight to logcat on the calling thread. A pipe drained by a reader
 * thread loses whatever is still in flight when exit() tears the process down,
 * which is exactly the refusal that explains a short run -- the message that
 * matters most is the one such a scheme drops.
 */
int log_write(void *cookie, const char *data, int size)
{
    if (size > 0)
        __android_log_print(ANDROID_LOG_INFO, static_cast<const char *>(cookie),
                            "%.*s", size, data);
    return size;
}

FILE *log_stream(const char *tag)
{
    FILE *stream = ::funopen(const_cast<char *>(tag), nullptr, log_write,
                             nullptr, nullptr);
    if (stream) ::setvbuf(stream, nullptr, _IOLBF, 0);
    return stream;
}

} // namespace

extern "C" void x2_android_log_stdio(void)
{
    static bool routed = false;
    if (routed) return;
    FILE *out = log_stream("x2native");
    FILE *err = log_stream("x2native");
    if (!out || !err) {
        if (out) ::fclose(out);
        if (err) ::fclose(err);
        return;
    }
    stdout = out;
    stderr = err;
    /* Line buffering would hold a refusal that does not end in a newline. */
    ::setvbuf(stderr, nullptr, _IONBF, 0);
    routed = true;
}

#else

extern "C" const char *x2_android_install_source(void)
{
    return nullptr;
}

extern "C" void x2_android_log_stdio(void)
{
}

#endif

#if defined(__ANDROID__)
extern "C" const char *x2_android_install_source(void)
{
    return install_source[0] ? install_source : nullptr;
}
#endif
