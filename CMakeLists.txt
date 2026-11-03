# Optional host applications consume installed Telemetry SDK components. MIT.
cmake_minimum_required(VERSION 3.21)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TELEMETRY_UX_VERSION LIMIT_COUNT 1)
project(TelemetryUX VERSION ${TELEMETRY_UX_VERSION} LANGUAGES CXX)
option(TELEMETRY_UX_BUILD_INSPECTOR "Build the Universal Inspector application" ON)
if(TELEMETRY_UX_BUILD_INSPECTOR)
    add_subdirectory(apps/inspector)
endif()
