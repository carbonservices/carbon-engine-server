cmake_minimum_required(VERSION 3.28)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

if(NOT DEFINED CARBON_ENGINE_VERSION OR CARBON_ENGINE_VERSION STREQUAL "")
    if(DEFINED ENV{CARBON_ENGINE_VERSION} AND NOT "$ENV{CARBON_ENGINE_VERSION}" STREQUAL "")
        set(CARBON_ENGINE_VERSION "$ENV{CARBON_ENGINE_VERSION}")
    elseif(DEFINED ENV{GITHUB_REF_NAME} AND "$ENV{GITHUB_REF_NAME}" MATCHES "^v?[0-9]")
        string(REGEX REPLACE "^v" "" CARBON_ENGINE_VERSION "$ENV{GITHUB_REF_NAME}")
    else()
        find_package(Git QUIET)
        if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" describe --tags --always
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE GIT_TAG_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(GIT_TAG_VER MATCHES "^v?([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
                set(CARBON_ENGINE_VERSION "${CMAKE_MATCH_1}")
            endif()
        endif()
    endif()
endif()

if(NOT DEFINED CARBON_ENGINE_VERSION OR CARBON_ENGINE_VERSION STREQUAL "")
    set(CARBON_ENGINE_VERSION "1.0.0")
endif()

string(REGEX REPLACE "^v" "" CARBON_ENGINE_VERSION "${CARBON_ENGINE_VERSION}")
if(CARBON_ENGINE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)$")
    set(CARBON_ENGINE_VERSION "${CARBON_ENGINE_VERSION}.0")
endif()

project(carbon-engine-server VERSION ${CARBON_ENGINE_VERSION} LANGUAGES C CXX)
add_compile_definitions(CARBON_ENGINE_VERSION="${CARBON_ENGINE_VERSION}")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(ARCH_DIR "x64")
else()
    set(ARCH_DIR "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(BUILD_ROOT "${CMAKE_SOURCE_DIR}/__Builds/${ARCH_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BUILD_ROOT}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BUILD_ROOT}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BUILD_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BUILD_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${BUILD_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${BUILD_ROOT}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BUILD_ROOT}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${BUILD_ROOT}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${BUILD_ROOT}/lib")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Headless server: only require non-GUI Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core HttpServer Sql Concurrent Network)
qt_standard_project_setup()

set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third-party" CACHE PATH "" FORCE)
include(FetchContent)

if(WIN32)
    find_program(NASM_EXECUTABLE nasm
        PATHS
            "C:/Program Files/NASM"
            "$ENV{LOCALAPPDATA}/Programs/NASM"
    )
    if(NOT NASM_EXECUTABLE)
        execute_process(COMMAND winget install --id NASM.NASM --exact --silent --accept-package-agreements --accept-source-agreements)
        unset(NASM_EXECUTABLE CACHE)
        set(ENV{PATH} "$ENV{PATH};C:\\Program Files\\NASM;$ENV{LOCALAPPDATA}\\Programs\\NASM;$ENV{LOCALAPPDATA}\\bin\\NASM;$ENV{LOCALAPPDATA}\\NASM")
        find_program(NASM_EXECUTABLE nasm REQUIRED
            PATHS
                "C:/Program Files/NASM"
                "$ENV{LOCALAPPDATA}/Programs/NASM"
                "$ENV{LOCALAPPDATA}/bin/NASM"
                "$ENV{LOCALAPPDATA}/NASM"
        )
    endif()
endif()

set(CMAKE_SKIP_INSTALL_RULES ON)

set(INJA_USE_EMBEDDED_JSON ON CACHE BOOL "" FORCE)
FetchContent_Declare(inja URL https://github.com/pantor/inja/archive/refs/tags/v3.4.0.tar.gz EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(inja)

FetchContent_Declare(llama GIT_REPOSITORY https://github.com/ggerganov/llama.cpp.git GIT_TAG 411d548515017ca75c2a12c39659b6e7e1551dd7 EXCLUDE_FROM_ALL)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)

find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit found (${CUDAToolkit_VERSION}). Enabling GGML_CUDA hardware acceleration.")
    set(GGML_CUDA ON CACHE BOOL "" FORCE)
else()
    message(STATUS "CUDA Toolkit not found. Building llama.cpp with CPU backend.")
    set(GGML_CUDA OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(llama)

if(WIN32)
    FetchContent_Declare(mkl_static URL https://api.nuget.org/v3-flatcontainer/intelmkl.static.win-x64/2026.1.0.226/intelmkl.static.win-x64.2026.1.0.226.nupkg DOWNLOAD_NAME mkl_static.zip EXCLUDE_FROM_ALL)
    FetchContent_Declare(mkl_devel URL https://api.nuget.org/v3-flatcontainer/intelmkl.devel.win-x64/2026.1.0.226/intelmkl.devel.win-x64.2026.1.0.226.nupkg DOWNLOAD_NAME mkl_devel.zip EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(mkl_static mkl_devel)

    set(MKL_INCLUDE_DIR "${mkl_devel_SOURCE_DIR}/build/native/include" CACHE PATH "" FORCE)
    set(MKL_LIB_DIR "${mkl_static_SOURCE_DIR}/build/native/win-x64" CACHE PATH "" FORCE)

    file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMKL.cmake" "
set(MKL_FOUND TRUE)
find_path(MKL_INCLUDE_DIRS NAMES mkl.h PATHS \"${MKL_INCLUDE_DIR}\" NO_DEFAULT_PATH)
find_library(MKL_INTEL_LP64 NAMES mkl_intel_lp64 PATHS \"${MKL_LIB_DIR}\" NO_DEFAULT_PATH)
find_library(MKL_SEQUENTIAL NAMES mkl_sequential PATHS \"${MKL_LIB_DIR}\" NO_DEFAULT_PATH)
find_library(MKL_CORE NAMES mkl_core PATHS \"${MKL_LIB_DIR}\" NO_DEFAULT_PATH)

if (NOT MKL_INCLUDE_DIRS OR NOT MKL_INTEL_LP64 OR NOT MKL_SEQUENTIAL OR NOT MKL_CORE)
    message(FATAL_ERROR \"Intel MKL libraries or headers could not be found under ${MKL_LIB_DIR}.\")
endif()

set(MKL_LIBRARIES \${MKL_INTEL_LP64} \${MKL_SEQUENTIAL} \${MKL_CORE})
if(NOT TARGET MKL::MKL)
    add_library(MKL::MKL INTERFACE IMPORTED)
    set_target_properties(MKL::MKL PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"\${MKL_INCLUDE_DIRS}\" INTERFACE_LINK_LIBRARIES \"\${MKL_LIBRARIES}\")
endif()
")
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMKL.cmake")
        file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMKL.cmake")
    endif()
    find_package(BLAS REQUIRED)
    find_package(LAPACK REQUIRED)
endif()

FetchContent_Declare(faiss GIT_REPOSITORY https://github.com/facebookresearch/faiss.git GIT_TAG v1.9.0 EXCLUDE_FROM_ALL)
set(FAISS_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(faiss)

if(WIN32)
    FetchContent_Declare(pdfium URL https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-win-x64.tgz EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(pdfium)
    set(PDFIUM_INCLUDE_DIRS ${pdfium_SOURCE_DIR}/include)
    set(PDFIUM_LIBRARIES ${pdfium_SOURCE_DIR}/lib/pdfium.dll.lib)
else()
    FetchContent_Declare(pdfium URL https://github.com/bblanchon/pdfium-binaries/releases/latest/download/pdfium-linux-x64.tgz EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(pdfium)
    set(PDFIUM_INCLUDE_DIRS ${pdfium_SOURCE_DIR}/include)
    set(PDFIUM_LIBRARIES ${pdfium_SOURCE_DIR}/lib/libpdfium.so)
endif()

set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(PNG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(ZLIB GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1 OVERRIDE_FIND_PACKAGE EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(ZLIB)
if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
set(ZLIB_LIBRARIES zlibstatic CACHE STRING "" FORCE)

FetchContent_Declare(JPEG GIT_REPOSITORY https://github.com/libjpeg-turbo/libjpeg-turbo.git GIT_TAG 2.1.5.1 OVERRIDE_FIND_PACKAGE EXCLUDE_FROM_ALL)
set(ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(ENABLE_STATIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JPEG)

set(JPEG_INCLUDE_DIR ${jpeg_SOURCE_DIR} ${jpeg_BINARY_DIR} CACHE PATH "" FORCE)
set(JPEG_LIBRARIES jpeg-static CACHE STRING "" FORCE)

FetchContent_Declare(PNG GIT_REPOSITORY https://github.com/glennrp/libpng.git GIT_TAG v1.6.43 OVERRIDE_FIND_PACKAGE EXCLUDE_FROM_ALL)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(PNG)

set(PNG_INCLUDE_DIRS ${png_SOURCE_DIR} ${png_BINARY_DIR} CACHE PATH "" FORCE)
set(PNG_LIBRARIES png_static CACHE STRING "" FORCE)

export(TARGETS zlibstatic jpeg-static png_static FILE "${CMAKE_BINARY_DIR}/DepsTargets.cmake")

FetchContent_Declare(Leptonica GIT_REPOSITORY https://github.com/DanBloomberg/leptonica.git GIT_TAG 1.83.1 OVERRIDE_FIND_PACKAGE EXCLUDE_FROM_ALL)
set(SW_BUILD OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB ON CACHE BOOL "" FORCE)
set(WITH_JPEG ON CACHE BOOL "" FORCE)
set(WITH_PNG ON CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_GIF OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_OPENJPEG OFF CACHE BOOL "" FORCE)
set(BUILD_PROG OFF CACHE BOOL "" FORCE)
set(Leptonica_DIR "${leptonica_SOURCE_DIR}" CACHE PATH "" FORCE)

FetchContent_MakeAvailable(Leptonica)
include_directories(${leptonica_SOURCE_DIR}/src ${leptonica_BINARY_DIR}/src)

set(Leptonica_FOUND TRUE CACHE BOOL "" FORCE)
set(Leptonica_INCLUDE_DIRS ${leptonica_SOURCE_DIR}/src ${leptonica_BINARY_DIR}/src CACHE PATH "" FORCE)
set(Leptonica_LIBRARIES leptonica CACHE STRING "" FORCE)

FetchContent_Declare(tesseract GIT_REPOSITORY https://github.com/tesseract-ocr/tesseract.git GIT_TAG 5.3.3 EXCLUDE_FROM_ALL)
set(BUILD_TRAINING_TOOLS OFF CACHE BOOL "" FORCE)
set(DISABLE_CURL ON CACHE BOOL "" FORCE)
set(DISABLE_ARCHIVE ON CACHE BOOL "" FORCE)
set(LEPT_TIFF_RESULT 0 CACHE INTERNAL "")
FetchContent_MakeAvailable(tesseract)

set(CMAKE_SKIP_INSTALL_RULES OFF)

foreach(_dep_bin 
    "${inja_BINARY_DIR}"
    "${llama_BINARY_DIR}"
    "${faiss_BINARY_DIR}"
    "${zlib_BINARY_DIR}"
    "${jpeg_BINARY_DIR}"
    "${png_BINARY_DIR}"
    "${leptonica_BINARY_DIR}"
    "${tesseract_BINARY_DIR}"
)
    if(_dep_bin)
        file(MAKE_DIRECTORY "${_dep_bin}")
        file(WRITE "${_dep_bin}/cmake_install.cmake" "# Dummy install script for third-party dependency\n")
    endif()
endforeach()

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/eng.traineddata")
    message(STATUS "Downloading Tesseract English language model...")
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/eng.traineddata"
        "${CMAKE_SOURCE_DIR}/eng.traineddata"
        SHOW_PROGRESS
    )
endif()

# Default to OFF for containerized deployments (uses Gemini Cloud by default)
option(CARBON_ENGINE_DOWNLOAD_MODELS "Download local LLM and embedding weights" OFF)
if(CARBON_ENGINE_DOWNLOAD_MODELS)
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/models")

    set(BGE_MODEL_FILE "${CMAKE_SOURCE_DIR}/models/bge-small-en-v1.5-f16.gguf")
    if(NOT EXISTS "${BGE_MODEL_FILE}")
        message(STATUS "Downloading BGE embedding model...")
        file(DOWNLOAD
            "https://huggingface.co/compendiumlabs/bge-small-en-v1.5-gguf/resolve/main/bge-small-en-v1.5-f16.gguf"
            "${BGE_MODEL_FILE}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS BGE_STATUS
        )
        list(GET BGE_STATUS 0 BGE_CODE)
        if(NOT BGE_CODE EQUAL 0)
            file(REMOVE "${BGE_MODEL_FILE}")
            message(FATAL_ERROR "Failed to download BGE embedding model: ${BGE_STATUS}")
        endif()
    endif()

    set(GEMMA_MODEL_FILE "${CMAKE_SOURCE_DIR}/models/gemma-2-2b-it-q4_k_m.gguf")
    if(NOT EXISTS "${GEMMA_MODEL_FILE}")
        message(STATUS "Downloading Gemma 2 2B generation model...")
        file(DOWNLOAD
            "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/main/gemma-2-2b-it-Q4_K_M.gguf"
            "${GEMMA_MODEL_FILE}"
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS GEMMA_STATUS
        )
        list(GET GEMMA_STATUS 0 GEMMA_CODE)
        if(NOT GEMMA_CODE EQUAL 0)
            file(REMOVE "${GEMMA_MODEL_FILE}")
            message(FATAL_ERROR "Failed to download Gemma generation model: ${GEMMA_STATUS}")
        endif()
    endif()
endif()

add_subdirectory(src)

