add_library(carbon-engine-gcloud STATIC
    config/GCloudConfig.h
    config/GCloudConfig.cpp
    rate_limiter/RequestPacer.h
    rate_limiter/RequestPacer.cpp
    embedding/GeminiEmbeddingClient.h
    embedding/GeminiEmbeddingClient.cpp
    client/GeminiClient.h
    client/GeminiClient.cpp
    client/GeminiRestTransport.h
    client/GeminiRestTransport.cpp
)

target_compile_features(carbon-engine-gcloud PUBLIC cxx_std_20)
target_compile_definitions(carbon-engine-gcloud PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(carbon-engine-gcloud PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(carbon-engine-gcloud PUBLIC
    carbon-engine-common
    carbon-engine-logging
    Qt6::Core
    Qt6::Network
)

