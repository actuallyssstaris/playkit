set(CMAKE_SYSTEM_NAME PlayStation)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}" CACHE PATH "PS5 payload SDK")
if(NOT PS5_PAYLOAD_SDK)
    message(FATAL_ERROR "PS5_PAYLOAD_SDK is not set")
endif()

set(CMAKE_C_COMPILER clang-18)
set(CMAKE_CXX_COMPILER clang++-18)
set(CMAKE_C_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_LINKER "${PS5_PAYLOAD_SDK}/bin/prospero-lld")
set(CMAKE_AR "${PS5_PAYLOAD_SDK}/bin/llvm-ar")
set(CMAKE_RANLIB "${PS5_PAYLOAD_SDK}/bin/llvm-ranlib")
set(CMAKE_NM "${PS5_PAYLOAD_SDK}/bin/llvm-nm")

set(CMAKE_SYSROOT "${PS5_PAYLOAD_SDK}")
set(CMAKE_C_FLAGS_INIT "-isystem ${PS5_PAYLOAD_SDK}/target/include -fno-stack-protector -fno-plt -femulated-tls -include /root/projects/PlayKit/platform/ps5_compat.h -isystem /root/projects/PlayKit/platform/include")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${PS5_PAYLOAD_SDK}/target/include/c++/v1 -isystem ${PS5_PAYLOAD_SDK}/target/include -fno-stack-protector -fno-plt -femulated-tls -include /root/projects/PlayKit/platform/ps5_compat.h -isystem /root/projects/PlayKit/platform/include")
set(PS5_LIB_DIR "${PS5_PAYLOAD_SDK}/target/lib")
set(PS5_LINK_FLAGS "-L${PS5_LIB_DIR} -Wl,--whole-archive -lc++ -lc++abi -lc -lunwind -Wl,--no-whole-archive -lkernel -lSceLibcInternal -ldl")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${PS5_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${PS5_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${PS5_LINK_FLAGS}")

# The SCE clang finds prospero-lld through PATH.  Pass -fuse-ld is rejected
# for this target, so put the SDK bin directory on PATH for every link.

# Most probe programs cannot link against the console libc.  Static archive
# probes are enough for configuration; real targets are linked by the app.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "${PS5_PAYLOAD_SDK}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
