add_library(carbon-engine-system STATIC
    SystemMonitor.h
    SystemMonitor.cpp
    SingleInstanceGuard.h
    WindowsService.h
    WindowsService.cpp
)

target_compile_features(carbon-engine-system PUBLIC cxx_std_20)

target_include_directories(carbon-engine-system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(carbon-engine-system PUBLIC
    carbon-engine-logging
    Qt6::Core
)

if(WIN32)
    target_link_libraries(carbon-engine-system PRIVATE dxgi pdh advapi32)
endif()

