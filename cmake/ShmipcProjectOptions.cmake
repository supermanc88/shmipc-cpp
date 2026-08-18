include_guard(GLOBAL)

function(shmipc_apply_project_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(SHMIPC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
        )
        if(SHMIPC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    if(SHMIPC_ENABLE_ASAN AND SHMIPC_ENABLE_TSAN)
        message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer cannot be enabled together")
    endif()

    set(sanitizers "")
    if(SHMIPC_ENABLE_ASAN)
        list(APPEND sanitizers address)
    endif()
    if(SHMIPC_ENABLE_UBSAN)
        list(APPEND sanitizers undefined)
    endif()
    if(SHMIPC_ENABLE_TSAN)
        list(APPEND sanitizers thread)
    endif()

    if(sanitizers)
        if(MSVC)
            message(FATAL_ERROR "The current sanitizer options require GCC or Clang")
        endif()
        list(JOIN sanitizers "," sanitizer_flags)
        target_compile_options(
            ${target}
            PRIVATE
                -fno-omit-frame-pointer
                "-fsanitize=${sanitizer_flags}"
        )
        target_link_options(${target} PRIVATE "-fsanitize=${sanitizer_flags}")
    endif()
endfunction()
