add_library(carbon-engine-common STATIC
    constants/CommonConstants.h
    db/Database.h
    db/Database.cpp
    report/BRSRSchema.h
    report/BRSRSchema.cpp
    report/FactStore.h
    report/FactStore.cpp
    report/ReportValidator.h
    report/ReportValidator.cpp
    report/ReportExporter.h
    report/ReportExporter.cpp
    report/ReportService.h
    report/ReportService.cpp
    report/JsonRepairEngine.h
    report/JsonRepairEngine.cpp
)

target_compile_features(carbon-engine-common PUBLIC cxx_std_20)
target_compile_definitions(carbon-engine-common PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

target_include_directories(carbon-engine-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(carbon-engine-common PUBLIC
    carbon-engine-logging
    Qt6::Sql
    inja
)

