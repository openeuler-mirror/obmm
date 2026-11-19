cmake_minimum_required(VERSION 3.10)

project(OBMM_ACCEL_SO C ASM)

# pthread_create/join/once: on glibc < 2.34 these live in libpthread,
# and a dlopen() host without libpthread loaded fails on the missing
# DT_NEEDED entry if we do not link it explicitly.
find_package(Threads REQUIRED)

# Read version from the repo VERSION file (same source as libobmm)
set(VERSION_FILE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../VERSION)
file(READ ${VERSION_FILE_PATH} OBMM_ACCEL_VERSION)
string(STRIP ${OBMM_ACCEL_VERSION} OBMM_ACCEL_VERSION)

string(REGEX REPLACE "([0-9]+)\.([0-9]+)\.([0-9]+)" "\\1" OBMM_ACCEL_VERSION_MAJOR ${OBMM_ACCEL_VERSION})
set(OBMM_ACCEL_SOVERSION ${OBMM_ACCEL_VERSION_MAJOR})

add_library(OBMM_ACCEL_SO SHARED
    memcpy_nc.S
    memcpy_nc_mt.c
)

set_target_properties(OBMM_ACCEL_SO PROPERTIES
    OUTPUT_NAME obmm-accel
    VERSION ${OBMM_ACCEL_VERSION}
    SOVERSION ${OBMM_ACCEL_SOVERSION}
    LINK_FLAGS "-s"
)

target_include_directories(OBMM_ACCEL_SO PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_compile_options(OBMM_ACCEL_SO PRIVATE
    -Wall -Wextra -fno-common -std=gnu11 -fPIC -fvisibility=hidden
    -Wuninitialized -Wno-error -Wno-error=format -Wundef -Wunused
    -Wdate-time -Wshadow -Wmissing-declarations -Wmissing-prototypes -Wstrict-prototypes
)

target_compile_definitions(OBMM_ACCEL_SO PRIVATE _GNU_SOURCE)

target_link_libraries(OBMM_ACCEL_SO PRIVATE Threads::Threads)

if(GCOV)
    target_compile_options(OBMM_ACCEL_SO PRIVATE
        -O0 -fprofile-arcs -ftest-coverage -fkeep-inline-functions)
    target_link_libraries(OBMM_ACCEL_SO PRIVATE gcov)
    # strip flag (-s, see LINK_FLAGS above) is incompatible with coverage
    # builds; the GCOV branch must rebuild without it
    set_target_properties(OBMM_ACCEL_SO PROPERTIES LINK_FLAGS "")
endif()

install(TARGETS OBMM_ACCEL_SO LIBRARY DESTINATION lib64 OPTIONAL)
install(FILES libobmm-accel.h DESTINATION include)
