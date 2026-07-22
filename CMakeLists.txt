cmake_minimum_required(VERSION 3.26)

project(AppVerse VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter Development.SABIModule QUIET)
if(TARGET Python3::SABIModule)
  set(APPVERSE_PYTHON_TARGET Python3::SABIModule)
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)
  set(APPVERSE_PYTHON_TARGET Python3::Module)

  if(WIN32)
    get_filename_component(APPVERSE_PYTHON_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    set(APPVERSE_PYTHON_ABI3_LIB "${APPVERSE_PYTHON_DIR}/libs/python3.lib")
    if(EXISTS "${APPVERSE_PYTHON_ABI3_LIB}")
      add_library(appverse_python_abi3 UNKNOWN IMPORTED)
      set_target_properties(appverse_python_abi3 PROPERTIES
        IMPORTED_LOCATION "${APPVERSE_PYTHON_ABI3_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${Python3_INCLUDE_DIRS}"
      )
      set(APPVERSE_PYTHON_TARGET appverse_python_abi3)
    endif()
  endif()
endif()

set(PYVERSE_WEBVIEW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/webview")
if(EXISTS "${PYVERSE_WEBVIEW_DIR}/CMakeLists.txt")
  add_subdirectory("${PYVERSE_WEBVIEW_DIR}" "${CMAKE_BINARY_DIR}/_deps/webview-build")
else()
  include(FetchContent)
  FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0)
  FetchContent_MakeAvailable(webview)
endif()

add_library(pyverse_native MODULE
  src/native/pyverse_module.cpp
)

target_link_libraries(pyverse_native PRIVATE
  ${APPVERSE_PYTHON_TARGET}
  webview::core_static
)

target_include_directories(pyverse_native PRIVATE
  "${Python3_INCLUDE_DIRS}"
)

set_target_properties(pyverse_native PROPERTIES
  PREFIX ""
  OUTPUT_NAME "_native"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/appverse"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/appverse"
)

if(Python3_SOSABI)
  set_target_properties(pyverse_native PROPERTIES SUFFIX "${Python3_SOSABI}")
elseif(NOT WIN32)
  set_target_properties(pyverse_native PROPERTIES SUFFIX ".abi3${CMAKE_SHARED_LIBRARY_SUFFIX}")
endif()

set(PYVERSE_RUNTIME_FILES)

if(WIN32)
  set_target_properties(pyverse_native PROPERTIES SUFFIX ".pyd")

  if(MINGW)
    target_link_options(pyverse_native PRIVATE -static-libgcc -static-libstdc++)

    get_filename_component(PYVERSE_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(PYVERSE_WINPTHREAD_DLL "${PYVERSE_MINGW_BIN_DIR}/libwinpthread-1.dll")
    if(EXISTS "${PYVERSE_WINPTHREAD_DLL}")
      list(APPEND PYVERSE_RUNTIME_FILES "${PYVERSE_WINPTHREAD_DLL}")
      add_custom_command(TARGET pyverse_native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${PYVERSE_WINPTHREAD_DLL}"
          "$<TARGET_FILE_DIR:pyverse_native>"
        VERBATIM)
    endif()
  endif()

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|AARCH64")
    set(PYVERSE_WEBVIEW2_ARCH "arm64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PYVERSE_WEBVIEW2_ARCH "x64")
  else()
    set(PYVERSE_WEBVIEW2_ARCH "x86")
  endif()

  set(PYVERSE_WEBVIEW2_LOADER
    "${CMAKE_BINARY_DIR}/_deps/microsoft_web_webview2-src/build/native/${PYVERSE_WEBVIEW2_ARCH}/WebView2Loader.dll")
  list(APPEND PYVERSE_RUNTIME_FILES "${PYVERSE_WEBVIEW2_LOADER}")

  add_custom_command(TARGET pyverse_native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${PYVERSE_WEBVIEW2_LOADER}"
      "$<TARGET_FILE_DIR:pyverse_native>"
    VERBATIM)
endif()

install(TARGETS pyverse_native
  LIBRARY DESTINATION appverse
  RUNTIME DESTINATION appverse
)

if(PYVERSE_RUNTIME_FILES)
  install(FILES ${PYVERSE_RUNTIME_FILES} DESTINATION appverse)
endif()
