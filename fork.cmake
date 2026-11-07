# fork.cmake -- SWOS United's own additions to the phone builds (OpenSWOS's
# packaging/mobile/CMakeLists.txt includes this file; docs/FORKING.md), as fork.mk is to the
# Makefile: SWOS-API's client (third_party/swos-api/client), the game's side of the online API.
file(GLOB SWOSAPI_SOURCES CONFIGURE_DEPENDS ${REPO}/third_party/swos-api/client/src/*.c)
if(NOT SWOSAPI_SOURCES)
    message(FATAL_ERROR "third_party/swos-api/client is empty: git submodule update --init third_party/swos-api")
endif()
list(APPEND FORK_SOURCES ${SWOSAPI_SOURCES})
list(APPEND FORK_INCLUDES ${REPO}/third_party/swos-api/client/include)

# The LZMA SDK's 7z decoder (third_party/lzma): SWOS 2020's custom challenges (.SCN).
file(GLOB LZMA_SOURCES CONFIGURE_DEPENDS ${REPO}/third_party/lzma/*.c)
list(APPEND FORK_SOURCES ${LZMA_SOURCES})
list(APPEND FORK_INCLUDES ${REPO}/third_party/lzma)
