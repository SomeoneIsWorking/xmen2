#include "android_document_tree.h"

#if defined(__ANDROID__)
#include "x2_log.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>

namespace x2::native {

namespace {

/* Attaches guest and host threads to the VM on first use and detaches them
 * when they exit, which ART requires of every thread it saw attach. */
class JniThreads {
public:
  JniThreads() { pthread_key_create(&key_, detach); }

  JNIEnv *environment(JavaVM *vm) {
    vm_ = vm;
    JNIEnv *environment = nullptr;
    const jint status =
        vm->GetEnv(reinterpret_cast<void **>(&environment), JNI_VERSION_1_6);
    if (status == JNI_OK) {
      return environment;
    }
    if (status != JNI_EDETACHED ||
        vm->AttachCurrentThread(&environment, nullptr) != JNI_OK) {
      return nullptr;
    }
    pthread_setspecific(key_, environment);
    return environment;
  }

private:
  static void detach(void *attached);

  pthread_key_t key_{};
  JavaVM *vm_ = nullptr;
};

JniThreads g_threads;

void JniThreads::detach(void *attached) {
  if (attached && g_threads.vm_) {
    g_threads.vm_->DetachCurrentThread();
  }
}

/* ParcelFileDescriptor's mode for open(2) flags. "w" truncates on some
 * providers and not others, so a write that keeps content asks for "rw". */
const char *descriptor_mode(int flags) {
  if ((flags & O_ACCMODE) == O_RDONLY) {
    return "r";
  }
  if (flags & O_APPEND) {
    return "wa";
  }
  return (flags & O_TRUNC) ? "rwt" : "rw";
}

class AndroidDocumentTreeFiles final : public HostFileTree {
public:
  AndroidDocumentTreeFiles(JavaVM *vm, jobject tree, jmethodID open,
                           jmethodID status, jmethodID children,
                           jmethodID make_directory, jmethodID remove,
                           jmethodID replace)
      : vm_(vm), tree_(tree), open_(open), status_(status), children_(children),
        make_directory_(make_directory), remove_(remove), replace_(replace) {}

  ~AndroidDocumentTreeFiles() override {
    if (JNIEnv *environment = g_threads.environment(vm_)) {
      environment->DeleteGlobalRef(tree_);
    }
  }

  AndroidDocumentTreeFiles(const AndroidDocumentTreeFiles &) = delete;
  AndroidDocumentTreeFiles &
  operator=(const AndroidDocumentTreeFiles &) = delete;

  int open(const std::string &relative, int flags) override {
    return call_int(relative, [&](JNIEnv *environment, jstring path) {
      jstring mode = environment->NewStringUTF(descriptor_mode(flags));
      return environment->CallIntMethod(
          tree_, open_, path, mode, (flags & O_CREAT) ? JNI_TRUE : JNI_FALSE,
          (flags & O_EXCL) ? JNI_TRUE : JNI_FALSE);
    });
  }

  int stat(const std::string &relative, struct stat *out) override {
    Frame frame(vm_);
    JNIEnv *environment = frame.environment();
    if (!environment) {
      return -EIO;
    }
    auto *answer = static_cast<jlongArray>(environment->CallObjectMethod(
        tree_, status_, environment->NewStringUTF(relative.c_str())));
    if (failed(environment, "status", relative)) {
      return -EIO;
    }
    if (!answer) {
      return -ENOENT;
    }
    jlong values[3] = {};
    environment->GetLongArrayRegion(answer, 0, 3, values);
    if (failed(environment, "status", relative)) {
      return -EIO;
    }
    std::memset(out, 0, sizeof *out);
    out->st_mode = values[0] ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    out->st_size = static_cast<off_t>(values[1]);
    out->st_mtim.tv_sec = static_cast<time_t>(values[2] / 1000);
    out->st_mtim.tv_nsec = static_cast<long>((values[2] % 1000) * 1000000);
    out->st_nlink = 1;
    return 0;
  }

  int list(const std::string &relative,
           std::vector<std::string> &names) override {
    Frame frame(vm_);
    JNIEnv *environment = frame.environment();
    if (!environment) {
      return -EIO;
    }
    auto *answer = static_cast<jobjectArray>(environment->CallObjectMethod(
        tree_, children_, environment->NewStringUTF(relative.c_str())));
    if (failed(environment, "children", relative)) {
      return -EIO;
    }
    if (!answer) {
      return -ENOENT;
    }
    const jsize count = environment->GetArrayLength(answer);
    names.clear();
    names.reserve(static_cast<size_t>(count));
    for (jsize item = 0; item < count; ++item) {
      auto *name = static_cast<jstring>(
          environment->GetObjectArrayElement(answer, item));
      const char *utf =
          name ? environment->GetStringUTFChars(name, nullptr) : nullptr;
      if (!utf) {
        return -EIO;
      }
      names.emplace_back(utf);
      environment->ReleaseStringUTFChars(name, utf);
      environment->DeleteLocalRef(name);
    }
    return 0;
  }

  int make_directory(const std::string &relative) override {
    return call_int(relative, [&](JNIEnv *environment, jstring path) {
      return environment->CallIntMethod(tree_, make_directory_, path);
    });
  }

  int remove(const std::string &relative, bool directory) override {
    return call_int(relative, [&](JNIEnv *environment, jstring path) {
      return environment->CallIntMethod(tree_, remove_, path,
                                        directory ? JNI_TRUE : JNI_FALSE);
    });
  }

  int replace(const std::string &from, const std::string &to) override {
    return call_int(from, [&](JNIEnv *environment, jstring path) {
      return environment->CallIntMethod(tree_, replace_, path,
                                        environment->NewStringUTF(to.c_str()));
    });
  }

private:
  /* One local-reference frame per call: guest threads stay attached for the
   * whole run, so references must not accumulate on them. */
  class Frame {
  public:
    explicit Frame(JavaVM *vm) : environment_(g_threads.environment(vm)) {
      if (environment_ && environment_->PushLocalFrame(16) != 0) {
        environment_->ExceptionClear();
        environment_ = nullptr;
      }
    }
    ~Frame() {
      if (environment_) {
        environment_->PopLocalFrame(nullptr);
      }
    }
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
    JNIEnv *environment() const { return environment_; }

  private:
    JNIEnv *environment_;
  };

  static bool failed(JNIEnv *environment, const char *operation,
                     const std::string &relative) {
    if (!environment->ExceptionCheck()) {
      return false;
    }
    environment->ExceptionDescribe();
    environment->ExceptionClear();
    x2_log_error("android document tree: %s(\"%s\") threw\n", operation,
                 relative.c_str());
    return true;
  }

  template <typename Call>
  int call_int(const std::string &relative, Call call) {
    Frame frame(vm_);
    JNIEnv *environment = frame.environment();
    if (!environment) {
      return -EIO;
    }
    jstring path = environment->NewStringUTF(relative.c_str());
    const jint result = path ? call(environment, path) : -ENOMEM;
    return failed(environment, "call", relative) ? -EIO : result;
  }

  JavaVM *vm_;
  jobject tree_;
  jmethodID open_;
  jmethodID status_;
  jmethodID children_;
  jmethodID make_directory_;
  jmethodID remove_;
  jmethodID replace_;
};

} // namespace

std::unique_ptr<HostFileTree> android_document_tree_backend(JNIEnv *environment,
                                                            jobject tree) {
  JavaVM *vm = nullptr;
  if (!tree || environment->GetJavaVM(&vm) != JNI_OK) {
    return nullptr;
  }
  jclass type = environment->GetObjectClass(tree);
  jmethodID open = environment->GetMethodID(
      type, "openDescriptor", "(Ljava/lang/String;Ljava/lang/String;ZZ)I");
  jmethodID status =
      environment->GetMethodID(type, "status", "(Ljava/lang/String;)[J");
  jmethodID children = environment->GetMethodID(
      type, "children", "(Ljava/lang/String;)[Ljava/lang/String;");
  jmethodID make_directory =
      environment->GetMethodID(type, "makeDirectory", "(Ljava/lang/String;)I");
  jmethodID remove =
      environment->GetMethodID(type, "delete", "(Ljava/lang/String;Z)I");
  jmethodID replace = environment->GetMethodID(
      type, "replace", "(Ljava/lang/String;Ljava/lang/String;)I");
  environment->DeleteLocalRef(type);
  if (environment->ExceptionCheck() || !open || !status || !children ||
      !make_directory || !remove || !replace) {
    environment->ExceptionClear();
    x2_log_error("android document tree: the framework class lacks its "
                 "native access methods\n");
    return nullptr;
  }
  return std::make_unique<AndroidDocumentTreeFiles>(
      vm, environment->NewGlobalRef(tree), open, status, children,
      make_directory, remove, replace);
}

} // namespace x2::native
#endif
