#include "android_bridge.h"
#include "../config/environment.h"
#include "d3d8_device.h"
#include "d3d8_drawcall.h"
#include "gpu_draw.h"
#include "gpu_draw_trace.h"
#include "install_picker.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#if defined(__ANDROID__)
#include <jni.h>

#include <android/log.h>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>

#include <lucent/platform_c.h>

namespace {

char install_source[4096];

bool copy_string(char *destination, size_t capacity, const char *source) {
  if (capacity == 0)
    return false;
  if (!source || !source[0]) {
    destination[0] = 0;
    return true;
  }
  const size_t length = std::strlen(source);
  if (length >= capacity)
    return false;
  std::memcpy(destination, source, length + 1);
  return true;
}

bool read_string(JNIEnv *environment, jstring value, char *destination,
                 size_t capacity) {
  if (!value)
    return copy_string(destination, capacity, nullptr);
  const char *utf = environment->GetStringUTFChars(value, nullptr);
  if (!utf)
    return false;
  const bool copied = copy_string(destination, capacity, utf);
  environment->ReleaseStringUTFChars(value, utf);
  return copied;
}

bool is_module_character(char character) {
  return (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9') || character == '.' ||
         character == '_' || character == '-';
}

/* This is intentionally a fixed diagnostic switch rather than an Activity
 * supplied environment map. The profiler is x86-runtime-specific and the
 * values give two useful heartbeats quickly without becoming player policy. */
bool configure_performance_trace(jboolean enabled) {
  if (enabled != JNI_TRUE) {
    x2_config_override_unset(kX2ConfigHotEp);
    x2_config_override_unset(kX2ConfigHeartbeat);
    return true;
  }
  return x2_config_override_set(kX2ConfigHotEp, "4096", 1) == 0 &&
         x2_config_override_set(kX2ConfigHeartbeat, "2", 1) == 0;
}

/* The first sufficiently busy frame captures the complete D3D8 state that
 * makes the visible menu backdrop. This keeps Android diagnostics specific to
 * an existing renderer instrument instead of accepting arbitrary environment.
 */
bool configure_draw_dump(jboolean enabled) {
  if (enabled == JNI_TRUE) {
    gpu_draw_trace_arm_busy_frame(100u);
    d3d8_device_trace_texture_factor(1);
    gpu_draw_diagnostic_disable_depth(1);
    gpu_texture_request_format_support_report();
    __android_log_print(ANDROID_LOG_INFO, "XMen2",
                        "armed renderer busy-frame dump and TFACTOR trace");
  } else {
    gpu_draw_trace_disarm_frame_dump();
    d3d8_device_trace_texture_factor(0);
    gpu_draw_diagnostic_disable_depth(0);
  }
  return true;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_someoneisworking_xmen2_XMen2SetupActivity_nativeConfigureStorage(
    JNIEnv *environment, jclass, jstring data_directory, jstring source,
    jboolean trace_files, jboolean trace_performance,
    jboolean trace_draw_dump) {
  char source_path[sizeof install_source];
  if (!read_string(environment, data_directory, source_path,
                   sizeof source_path) ||
      !configure_performance_trace(trace_performance) ||
      !configure_draw_dump(trace_draw_dump))
    return JNI_FALSE;
  if (!lucent_platform_set_user_data_directory(source_path))
    return JNI_FALSE;
  if (!read_string(environment, source, install_source, sizeof install_source))
    return JNI_FALSE;
  if (trace_files == JNI_TRUE) {
    if (x2_config_override_set(kX2ConfigFiles, "1", 1) != 0)
      return JNI_FALSE;
  } else {
    x2_config_override_unset(kX2ConfigFiles);
  }
  /* The existing guest input path already knows how to create and map the
   * SDL virtual pad. Android supplies touch actions to that same pad, so
   * there is one binding path instead of a second mobile-only input stack. */
  return x2_config_override_set(kX2ConfigVirtualPad, "1", 1) == 0 ? JNI_TRUE
                                                                  : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_someoneisworking_xmen2_XMen2SetupActivity_nativeValidateInstall(
    JNIEnv *environment, jclass, jstring source, jstring archive_destination) {
  char source_path[sizeof install_source];
  char destination[sizeof install_source];
  char reason[512];
  if (!read_string(environment, source, source_path, sizeof source_path) ||
      !read_string(environment, archive_destination, destination,
                   sizeof destination))
    return JNI_FALSE;
  return x2_install_picker_prepare_selection(
             source_path, destination[0] ? destination : nullptr, reason,
             sizeof reason)
             ? JNI_TRUE
             : JNI_FALSE;
}

namespace {

std::thread *log_reader;

void logcat_reader(int descriptor) {
  std::array<char, 1024> bytes;
  std::string pending;
  for (;;) {
    const ssize_t count = ::read(descriptor, bytes.data(), bytes.size());
    if (count == 0)
      break;
    if (count < 0) {
      if (errno == EINTR)
        continue;
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

void stop_log_router() {
  ::fflush(nullptr);
  ::close(STDOUT_FILENO);
  ::close(STDERR_FILENO);
  if (log_reader && log_reader->joinable())
    log_reader->join();
}

} // namespace

extern "C" void x2_android_log_stdio(void) {
  static bool routed = false;
  if (routed)
    return;
  int pipe_descriptors[2];
  if (::pipe(pipe_descriptors) != 0)
    return;
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

extern "C" const char *x2_android_install_source(void) { return nullptr; }

extern "C" void x2_android_log_stdio(void) {}

#endif

#if defined(__ANDROID__)
extern "C" const char *x2_android_install_source(void) {
  return install_source[0] ? install_source : nullptr;
}
#endif
