set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build both versions as shared libraries so Binary Ninja can open them
# directly. Using SHARED avoids needing a main() and keeps the binaries
# small and focused on the functions under test.
#
# Compile with -O1 to get meaningful disassembly without excessive inlining,
# but without the noise of -O0's stack-spill scaffolding.

foreach(ver v1 v2)
    add_library(target_${ver} SHARED src/target_${ver}.c)
    target_compile_options(target_${ver} PRIVATE
        -O1
        -fno-omit-frame-pointer
        -fvisibility=default      # keep all symbols visible
        -g                        # include debug info for BN's type recovery
    )
    set_target_properties(target_${ver} PROPERTIES
        OUTPUT_NAME "target_${ver}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin"
    )

    # Stripped variant: hidden visibility exports nothing, and strip removes the
    # local symbol table, so Binary Ninja sees the functions only as sub_<addr>.
    # Used to prove the matcher is symbol-independent.
    add_library(target_${ver}_stripped SHARED src/target_${ver}.c)
    target_compile_options(target_${ver}_stripped PRIVATE
        -O1 -fno-omit-frame-pointer -fvisibility=hidden -g)
    set_target_properties(target_${ver}_stripped PROPERTIES
        OUTPUT_NAME "target_${ver}_stripped"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
    add_custom_command(TARGET target_${ver}_stripped POST_BUILD
        COMMAND strip -x $<TARGET_FILE:target_${ver}_stripped>
        COMMENT "Stripping local symbols from target_${ver}_stripped")
endforeach()

# Headless end-to-end test for the diff engine. Links the Binary Ninja API/core
# and the (Qt-free) engine sources into a standalone executable. Build and run:
#   cmake --build build --target diff_harness
#   ./build/tests/bin/diff_harness \
#       build/tests/bin/libtarget_v1.dylib build/tests/bin/libtarget_v2.dylib
add_executable(diff_harness
    harness/diff_harness.cpp
    ${CMAKE_SOURCE_DIR}/src/bifrostfeatures.cpp
    ${CMAKE_SOURCE_DIR}/src/bifrostmatch.cpp
    ${CMAKE_SOURCE_DIR}/src/bifrostdiffengine.cpp
    ${CMAKE_SOURCE_DIR}/src/bifrostdiffstore.cpp
    ${CMAKE_SOURCE_DIR}/src/bifrostexport.cpp)
target_include_directories(diff_harness PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(diff_harness binaryninjaapi)
if(NOT WIN32)
    target_link_libraries(diff_harness dl)
endif()
set_target_properties(diff_harness PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bin")
