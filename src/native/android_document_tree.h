/* The player's Android install folder as a host file tree: every call goes to
 * the shared framework's io.github.someoneisworking.android.AndroidDocumentTree
 * over JNI, which answers from its document index and opens descriptors
 * through the ContentResolver. */
#pragma once

#if defined(__ANDROID__)
#include "host_files.h"

#include <jni.h>
#include <memory>

namespace x2::native {

/* Where the folder appears to host_files. No such directory exists. */
inline constexpr const char *kAndroidDocumentTreeRoot = "/document-tree";

/* A backend over `tree`, an AndroidDocumentTree; nullptr if its methods are
 * missing. Call on a thread whose class loader sees the framework. */
std::unique_ptr<HostFileTree> android_document_tree_backend(JNIEnv *environment,
                                                            jobject tree);

} // namespace x2::native
#endif
