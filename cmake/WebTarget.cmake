# The browser main thread owns events. All guest execution, blocking native
# calls and synchronous per-block WebAssembly compilation run on a pthread.
target_sources(x2native PRIVATE src/web/web_main.cpp src/web/browser_log.cpp
                   src/web/web_request.cpp)
set_source_files_properties(src/native/x2native.c PROPERTIES
    COMPILE_DEFINITIONS main=x2native_main)
if(NOT X2_WEB_PORT_SOURCE)
    message(FATAL_ERROR "x2native: web build requires shared/web-port source")
endif()
include("${X2_WEB_PORT_SOURCE}/cmake/WebRuntime.cmake")
target_link_libraries(x2native PRIVATE web_port::runtime)
target_link_options(x2native PRIVATE
    -pthread -sPROXY_TO_PTHREAD=1 -sPTHREAD_POOL_SIZE=16 -sASYNCIFY=1
    -sOFFSCREENCANVAS_SUPPORT=1 "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    -sWASMFS=1 -sFORCE_FILESYSTEM=1
    # Static data plus the main stack currently require 31 MiB. Shared memory
    # cannot use INITIAL_HEAP; reserve room for startup and grow on demand.
    -sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=2MB -sDEFAULT_PTHREAD_STACK_SIZE=2MB
    -sALLOW_TABLE_GROWTH=1 -sEXIT_RUNTIME=1
    -sENVIRONMENT=web,worker -sASSERTIONS=1
    -sEXPORTED_RUNTIME_METHODS=callMain
    --emit-symbol-map
    "--preload-file=${CMAKE_BINARY_DIR}/ui@/ui")
set_target_properties(x2native PROPERTIES SUFFIX ".js")
