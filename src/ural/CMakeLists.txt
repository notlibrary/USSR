cmake_minimum_required(VERSION 3.16)
project(ural VERSION 0.1.0 LANGUAGES C)

option(URAL_STATIC_DEPS "Prefer static SQLite, cURL, OpenSSL, and zlib libraries" ON)
if(URAL_STATIC_DEPS)
    set(CURL_USE_STATIC_LIBS ON)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(ZLIB_USE_STATIC_LIBS TRUE)
endif()

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL 1.1 REQUIRED)
find_package(ZLIB REQUIRED)

add_library(ural_core STATIC
    native/ural_db.c
    native/ural_archive.c
    native/ural_manifest.c
    native/ural_url.c)
target_include_directories(ural_core PUBLIC native)
target_compile_features(ural_core PUBLIC c_std_11)
target_link_libraries(ural_core PUBLIC
    SQLite::SQLite3
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB)

add_executable(ural native/ural.c)
target_link_libraries(ural PRIVATE ural_core)

# Windows: embed the mountain icon (ural.ico, made by icon.py) in ural.exe.
if(WIN32)
    enable_language(RC)
    target_sources(ural PRIVATE ural.rc)
    set_source_files_properties(ural.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ural.ico)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_ural_db tests/test_db.c)
    target_link_libraries(test_ural_db PRIVATE ural_core)
    add_test(NAME ural_db COMMAND test_ural_db)

    add_executable(test_ural_archive tests/test_archive.c)
    target_link_libraries(test_ural_archive PRIVATE ural_core)
    add_test(NAME ural_archive COMMAND test_ural_archive)

    add_executable(test_ural_manifest tests/test_manifest.c)
    target_link_libraries(test_ural_manifest PRIVATE ural_core)
    add_test(NAME ural_manifest COMMAND test_ural_manifest)

    add_executable(test_ural_url tests/test_url.c)
    target_link_libraries(test_ural_url PRIVATE ural_core)
    add_test(NAME ural_url COMMAND test_ural_url)
endif()

install(TARGETS ural RUNTIME DESTINATION bin)
