# A private backend for the library's own tests, kept outside src/ as a
# console's would be: maul-audio's build includes this file when
# MAUL_AUDIO_PRIVATE_BACKEND names this directory.
target_sources(maul-audio PRIVATE "${CMAKE_CURRENT_LIST_DIR}/private_backend.c")
target_include_directories(maul-audio PRIVATE "${PROJECT_SOURCE_DIR}/src")
