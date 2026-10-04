# Wasmtime runs WebAssembly mods. Its prebuilt C API release is downloaded for
# the target platform, or taken from MODLOCK_WASMTIME_DIR when set, and
# provided as the imported shared library wasmtime::wasmtime.
include(FetchContent)

set(MODLOCK_WASMTIME_VERSION 49.0.2)
set(MODLOCK_WASMTIME_DIR "" CACHE PATH "Extracted Wasmtime C API release to use instead of downloading one")

if(MINGW)
  set(wasmtime_target x86_64-mingw-c-api.zip)
  set(wasmtime_sha256 113603c5627adc5ea89d80554434662198aa205d248ba7837c609604ee87ff1c)
elseif(WIN32)
  set(wasmtime_target x86_64-windows-c-api.zip)
  set(wasmtime_sha256 928dad67acbf4a4f97be8d528b271ac72f8e329da21e327b0724d3af86fe3f44)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  set(wasmtime_target aarch64-macos-c-api.tar.xz)
  set(wasmtime_sha256 f7000ab1661495d09b9dc87a11b356941b4e6097f295434bcb30a8cf1393b0d5)
elseif(APPLE)
  set(wasmtime_target x86_64-macos-c-api.tar.xz)
  set(wasmtime_sha256 1a1fd4bcec77d65bab1bdd4c38e4168825b7679c2f105bf573c4ef1cf5e779b1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(wasmtime_target aarch64-linux-c-api.tar.xz)
  set(wasmtime_sha256 a97a874152b5fb49c4720b39b96a71d00ba61eab5ad59976163b7ddd1a3b14c0)
else()
  set(wasmtime_target x86_64-linux-c-api.tar.xz)
  set(wasmtime_sha256 4818e139aeb83b7adbaaf41b267bf18992c18463ae26d1a9acdcfc63ea9eb316)
endif()

if(MODLOCK_WASMTIME_DIR)
  set(wasmtime_dir ${MODLOCK_WASMTIME_DIR})
else()
  FetchContent_Declare(wasmtime
    URL https://github.com/bytecodealliance/wasmtime/releases/download/v${MODLOCK_WASMTIME_VERSION}/wasmtime-v${MODLOCK_WASMTIME_VERSION}-${wasmtime_target}
    URL_HASH SHA256=${wasmtime_sha256}
    DOWNLOAD_EXTRACT_TIMESTAMP ON)
  FetchContent_MakeAvailable(wasmtime)
  set(wasmtime_dir ${wasmtime_SOURCE_DIR})
endif()

add_library(wasmtime::wasmtime SHARED IMPORTED GLOBAL)
set_target_properties(wasmtime::wasmtime PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES ${wasmtime_dir}/include)
if(MINGW)
  set(MODLOCK_WASMTIME_LIBRARY ${wasmtime_dir}/lib/wasmtime.dll)
  set_target_properties(wasmtime::wasmtime PROPERTIES
    IMPORTED_LOCATION ${MODLOCK_WASMTIME_LIBRARY}
    IMPORTED_IMPLIB ${wasmtime_dir}/lib/libwasmtime.dll.a)
elseif(WIN32)
  set(MODLOCK_WASMTIME_LIBRARY ${wasmtime_dir}/lib/wasmtime.dll)
  set_target_properties(wasmtime::wasmtime PROPERTIES
    IMPORTED_LOCATION ${MODLOCK_WASMTIME_LIBRARY}
    IMPORTED_IMPLIB ${wasmtime_dir}/lib/wasmtime.dll.lib)
elseif(APPLE)
  set(MODLOCK_WASMTIME_LIBRARY ${wasmtime_dir}/lib/libwasmtime.dylib)
  set_target_properties(wasmtime::wasmtime PROPERTIES IMPORTED_LOCATION ${MODLOCK_WASMTIME_LIBRARY})
else()
  set(MODLOCK_WASMTIME_LIBRARY ${wasmtime_dir}/lib/libwasmtime.so)
  set_target_properties(wasmtime::wasmtime PROPERTIES
    IMPORTED_LOCATION ${MODLOCK_WASMTIME_LIBRARY}
    IMPORTED_SONAME libwasmtime.so)
endif()
