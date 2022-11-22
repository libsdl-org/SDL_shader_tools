find_program(RE2C_BINARY
    NAMES re2c
    DOC "Path to re2c command line app: https://re2c.org/"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(re2c
    FOUND_VAR re2c_FOUND
    REQUIRED_VARS RE2C_BINARY
)

if(re2c_FOUND)
    if(NOT TARGET re2c::re2c)
        add_executable(re2c::re2c IMPORTED)
        set_property(TARGET re2c::re2c PROPERTY IMPORTED_LOCATION "${RE2C_BINARY}")
    endif()
endif()

mark_as_advanced(RE2C_BINARY)
