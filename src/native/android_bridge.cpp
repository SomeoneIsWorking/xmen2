#include "android_bridge.h"

#include <cstring>
#include <cstdlib>

#if defined(__ANDROID__)
#include <jni.h>

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

#else

extern "C" const char *x2_android_install_source(void)
{
    return nullptr;
}

#endif

#if defined(__ANDROID__)
extern "C" const char *x2_android_install_source(void)
{
    return install_source[0] ? install_source : nullptr;
}
#endif
