#include "android_bridge.h"
#include "install_picker.h"

#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <array>
#include <string>
#include <thread>

#if defined(__ANDROID__)
#include <jni.h>

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdio>

#include <lucent/platform_c.h>

namespace {

char install_source[4096];
constexpr size_t trace_arguments_capacity = 256;
char trace_arguments[trace_arguments_capacity];

bool copy_string(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) return false;
    if (!source || !source[0]) {
        destination[0] = 0;
        return true;
    }
    const size_t length = std::strlen(source);
    if (length >= capacity) return false;
    std::memcpy(destination, source, length + 1);
    return true;
}

bool read_string(JNIEnv *environment, jstring value, char *destination, size_t capacity)
{
    if (!value) return copy_string(destination, capacity, nullptr);
    const char *utf = environment->GetStringUTFChars(value, nullptr);
    if (!utf) return false;
    const bool copied = copy_string(destination, capacity, utf);
    environment->ReleaseStringUTFChars(value, utf);
    return copied;
}

bool is_module_character(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') ||
           character == '.' || character == '_' || character == '-';
}

bool is_hex_character(char character)
{
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

/* x86rt_native's trace parser accepts an optional module qualifier and an
 * eight-hex-digit linked entry point. Keep Android's debug Intent narrowly
 * scoped to that grammar: an Activity must not become a generic setenv bridge. */
bool valid_trace_watch(const char *arguments, jint cap)
{
    if (!arguments || !arguments[0]) return cap == 0;
    if (cap < 1 || cap > 32) return false;

    int watches = 0;
    for (const char *token = arguments; *token;) {
        const char *end = token;
        const char *colon = nullptr;
        while (*end && *end != ',') {
            if (*end == ':') {
                if (colon) return false;
                colon = end;
            }
            ++end;
        }
        const char *address = colon ? colon + 1 : token;
        if (address + 3 > end || address[0] != '0' || address[1] != 'x')
            return false;
        if (colon) {
            const size_t module_size = static_cast<size_t>(colon - token);
            if (module_size == 0 || module_size >= 24) return false;
            for (const char *character = token; character < colon; ++character)
                if (!is_module_character(*character)) return false;
        }
        const size_t hexadecimal_size = static_cast<size_t>(end - (address + 2));
        if (hexadecimal_size == 0 || hexadecimal_size > 8) return false;
        for (const char *character = address + 2; character < end; ++character)
            if (!is_hex_character(*character)) return false;
        if (++watches > 16) return false;
        if (!*end) {
            token = end;
        } else {
            token = end + 1;
            if (!*token) return false;
        }
    }
    return true;
}

bool configure_trace_watch(const char *arguments, jint cap)
{
    if (!valid_trace_watch(arguments, cap)) return false;
    if (!arguments || !arguments[0]) {
        ::unsetenv("X2_ARGS");
        ::unsetenv("X2_ARGS_MAX");
        return true;
    }
    char cap_text[12];
    std::snprintf(cap_text, sizeof cap_text, "%d", static_cast<int>(cap));
    if (::setenv("X2_ARGS", arguments, 1) != 0) return false;
    if (::setenv("X2_ARGS_MAX", cap_text, 1) == 0) return true;
    ::unsetenv("X2_ARGS");
    return false;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_someoneisworking_xmen2_XMen2SetupActivity_nativeConfigureStorage(
    JNIEnv *environment, jclass, jstring data_directory, jstring source,
    jstring trace_watch, jint trace_cap, jboolean trace_files)
{
    char source_path[sizeof install_source];
    if (!read_string(environment, data_directory, source_path, sizeof source_path) ||
        !read_string(environment, trace_watch, trace_arguments,
                     sizeof trace_arguments) ||
        !configure_trace_watch(trace_arguments, trace_cap)) return JNI_FALSE;
    if (!lucent_platform_set_user_data_directory(source_path)) return JNI_FALSE;
    if (!read_string(environment, source, install_source, sizeof install_source))
        return JNI_FALSE;
    if (trace_files == JNI_TRUE) {
        if (::setenv("X2_FILES", "1", 1) != 0) return JNI_FALSE;
    } else {
        ::unsetenv("X2_FILES");
    }
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
    if (!read_string(environment, source, source_path, sizeof source_path) ||
        !read_string(environment, archive_destination, destination,
                     sizeof destination))
        return JNI_FALSE;
    return x2_install_picker_prepare_selection(
               source_path, destination[0] ? destination : nullptr,
               reason, sizeof reason)
               ? JNI_TRUE : JNI_FALSE;
}

namespace {

std::thread *log_reader;

void logcat_reader(int descriptor)
{
    std::array<char, 1024> bytes;
    std::string pending;
    for (;;) {
        const ssize_t count = ::read(descriptor, bytes.data(), bytes.size());
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            break;
        }
        pending.append(bytes.data(), static_cast<size_t>(count));
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            __android_log_write(ANDROID_LOG_INFO, "x2native",
                                pending.substr(0, newline).c_str());
            pending.erase(0, newline + 1);
        }
    }
    if (!pending.empty())
        __android_log_write(ANDROID_LOG_INFO, "x2native", pending.c_str());
    ::close(descriptor);
}

void stop_log_router()
{
    ::fflush(nullptr);
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);
    if (log_reader && log_reader->joinable()) log_reader->join();
}

} // namespace

extern "C" void x2_android_log_stdio(void)
{
    static bool routed = false;
    if (routed) return;
    int pipe_descriptors[2];
    if (::pipe(pipe_descriptors) != 0) return;
    if (::dup2(pipe_descriptors[1], STDOUT_FILENO) < 0 ||
        ::dup2(pipe_descriptors[1], STDERR_FILENO) < 0) {
        ::close(pipe_descriptors[0]);
        ::close(pipe_descriptors[1]);
        return;
    }
    ::close(pipe_descriptors[1]);
    /* The reader receives every completed write. On normal exit the atexit
     * hook flushes, closes, and joins it before process teardown. */
    log_reader = new std::thread(logcat_reader, pipe_descriptors[0]);
    ::setvbuf(stdout, nullptr, _IOLBF, 0);
    ::setvbuf(stderr, nullptr, _IONBF, 0);
    ::atexit(stop_log_router);
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
