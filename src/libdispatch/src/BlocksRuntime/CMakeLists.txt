
add_library(BlocksRuntime
  data.c
  runtime.c)
if(WIN32)
  target_sources(BlocksRuntime PRIVATE
    BlocksRuntime.def)

  if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(BlocksRuntime PUBLIC
      BlocksRuntime_STATIC)
    target_compile_options(BlocksRuntime PUBLIC
      "$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:$<$<BOOL:${MSVC}>:-Xclang >-static-libclosure>"
      $<$<COMPILE_LANGUAGE:Swift>:SHELL:-Xcc -static-libclosure>)
  endif()
endif()

target_include_directories(BlocksRuntime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR})
if(HAVE_OBJC AND CMAKE_DL_LIBS)
  target_link_libraries(BlocksRuntime PUBLIC
    ${CMAKE_DL_LIBS})
endif()

if(LINKER_SUPPORTS_BUILD_ID)
  target_link_options(BlocksRuntime PRIVATE "LINKER:--build-id=sha1")
endif()

add_library(BlocksRuntime::BlocksRuntime ALIAS BlocksRuntime)

# nextbsd#378: ship the base Blocks runtime as libsystem_blocks.so (its
# Apple-canonical name), NOT libBlocksRuntime.so — symmetric to the
# libsystem_dispatch rename. Otherwise BlocksRuntime keeps colliding with
# Gershwin's own bundled libBlocksRuntime.so even after libdispatch is renamed,
# and Gershwin would still need a RUNPATH shadow for it. OUTPUT_NAME renames the
# object + DT_SONAME; base consumers link -l:libsystem_blocks.so. The name is
# NextBSD-owned; Gershwin's separate bundled BlocksRuntime is untouched.
if(NOT APPLE AND NOT WIN32)
  set_target_properties(BlocksRuntime PROPERTIES OUTPUT_NAME system_blocks)
endif()

install(FILES Block.h
        DESTINATION ${INSTALL_BLOCK_HEADERS_DIR})
if(INSTALL_PRIVATE_HEADERS)
  install(FILES Block_private.h
          DESTINATION ${INSTALL_BLOCK_HEADERS_DIR})
endif()
set_property(GLOBAL APPEND PROPERTY DISPATCH_EXPORTS BlocksRuntime)
install(TARGETS BlocksRuntime
        EXPORT dispatchExports
        ARCHIVE DESTINATION ${INSTALL_TARGET_DIR}
        LIBRARY DESTINATION ${INSTALL_TARGET_DIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
