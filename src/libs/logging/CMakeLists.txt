add_library(carbon-engine-logging STATIC
    LogLevel.h
    Logger.h Logger.cpp
)

target_compile_features(carbon-engine-logging PUBLIC cxx_std_20)

target_include_directories(carbon-engine-logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(carbon-engine-logging PUBLIC Qt6::Core)
