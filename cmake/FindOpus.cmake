# AOO asks find_package(Opus) even when the parent already builds it.
if(TARGET Opus::opus)
    set(Opus_FOUND TRUE)
    set(OPUS_FOUND TRUE)
    return()
endif()
find_package(Opus CONFIG REQUIRED)
