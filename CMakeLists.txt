cmake_minimum_required(VERSION 3.14)

project(Update LANGUAGES C)

#########
# Build #
#########

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

set(CMAKE_INSTALL_LOCALSTATEDIR "var" CACHE STRING "Local state directory.")
set(CMAKE_INSTALL_RUNSTATEDIR "${CMAKE_INSTALL_LOCALSTATEDIR}/run" CACHE STRING "Run state directory.")

set(CONFIG_DEFAULT_HNY_PREFIX "hub" CACHE STRING "Honey prefix to update.")
set(CONFIG_DEFAULT_SNAPSHOT_DIRECTORY "${CMAKE_INSTALL_RUNSTATEDIR}/update" CACHE STRING "Location of current and pending snapshots.")
set(CONFIG_DEFAULT_FETCH_SNAPSHOT_BUFFER_CAPACITY 4096 CACHE STRING "Default capacity of the buffer used to fetch the remote snapshot.")
set(CONFIG_DEFAULT_SET_CAPACITY 1024 CACHE STRING "Default capacity in bytes of the set common structure.")

find_package(CURL 7.64.1 REQUIRED)

find_path(HONEY_INCLUDE_DIRS hny.h REQUIRED)
find_library(HONEY_LIBRARIES hny REQUIRED)

configure_file(include/config.h.in include/config.h)
include_directories(
	${PROJECT_BINARY_DIR}/include include
	${CURL_INCLUDE_DIRS} ${HONEY_INCLUDE_DIRS}
)

file(GLOB_RECURSE UPDATE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/update/*.c)
add_executable(update ${UPDATE_SOURCES})

target_link_libraries(update PUBLIC ${CURL_LIBRARIES} ${HONEY_LIBRARIES})

###########
# Install #
###########

install(TARGETS update RUNTIME)

