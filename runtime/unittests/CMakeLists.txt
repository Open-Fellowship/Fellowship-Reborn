# Ordinary console programs, so they run without the game and ctest can drive them.
#
# Each test is unittests/<name>.c built against the REAL common library, never a stub of it,
# because a stub only proves the stub. unittest.c is the shared pass/fail harness. The whole
# directory inherits the project's /W4 /WX, so a test that does not compile clean fails the build
# exactly like the code it checks.
#
#   add_common_test(<name>)

function(add_common_test name)
    add_executable(${name}_test ${name}.c unittest.c)
    target_link_libraries(${name}_test PRIVATE fellowship_reborn_common)
    add_test(NAME ${name} COMMAND ${name}_test)
endfunction()

# Pure arithmetic and byte layout: the little assembler behind every stub.
add_common_test(emit)

# The readable-range and range-arithmetic guards, against memory the test controls.
add_common_test(memory)

# The verify-before-write core, including a call redirect that is built, executed, redirected and
# executed again to prove the guard the first audit asked for actually fires under live code.
add_common_test(patch)
