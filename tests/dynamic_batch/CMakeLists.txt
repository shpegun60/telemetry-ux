# Controlled presentation/admission tests; real Session execution is tested separately. MIT.
cmake_minimum_required(VERSION 3.21)
project(telemetry_dynamic_batch_checks LANGUAGES CXX)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)
find_package(Telemetry 0.1.0 EXACT CONFIG REQUIRED COMPONENTS core encoded_server encoded_backend)
find_package(TelemetryUX 0.1.0 EXACT CONFIG REQUIRED COMPONENTS dynamic_configurator)
qt_standard_project_setup(REQUIRES 6.4)
qt_add_executable(telemetry_dynamic_batch_check main.cpp)
target_compile_features(telemetry_dynamic_batch_check PRIVATE cxx_std_20)
target_compile_definitions(telemetry_dynamic_batch_check PRIVATE QT_NO_KEYWORDS)
target_link_libraries(telemetry_dynamic_batch_check PRIVATE TelemetryUX::dynamic_configurator
    Telemetry::core Telemetry::encoded_server Telemetry::encoded_backend Qt6::Test)
enable_testing()
add_test(NAME dynamic_batch COMMAND telemetry_dynamic_batch_check)
