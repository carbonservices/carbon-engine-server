add_library(carbon-engine-offline STATIC
    ingestion/DocumentProcessor.h
    ingestion/DocumentProcessor.cpp
    inference/LlamaEngine.h
    inference/LlamaEngine.cpp
    vector/VectorIndex.h
    vector/VectorIndex.cpp
    vector/VectorIndexRegistry.h
    vector/VectorIndexRegistry.cpp
)

target_compile_features(carbon-engine-offline PUBLIC cxx_std_20)
target_compile_definitions(carbon-engine-offline PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(carbon-engine-offline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PDFIUM_INCLUDE_DIRS}
    ${tesseract_BINARY_DIR}/include
    ${leptonica_SOURCE_DIR}/src
    ${leptonica_BINARY_DIR}/src
)

target_link_libraries(carbon-engine-offline PUBLIC
    carbon-engine-common
    carbon-engine-system
    carbon-engine-logging
    Qt6::Core
    Qt6::Concurrent
    llama
    faiss
    ${PDFIUM_LIBRARIES}
    libtesseract
)

