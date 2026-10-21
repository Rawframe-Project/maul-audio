# In-tree samples: small programs built with the library, each with the
# part it uses.

if(MAUL_AUDIO_DEVICE)
    add_executable(sample_devices devices.c)
    target_link_libraries(sample_devices PRIVATE maul-audio)
    maul_apply_flags(sample_devices)
    if(EMSCRIPTEN)
        # A page that prints into its own text box; served from
        # localhost or https, as the microphone needs.
        set_target_properties(sample_devices PROPERTIES SUFFIX ".html")
    endif()
endif()

if(MAUL_AUDIO_SPATIAL)
    add_executable(sample_binaural binaural.c)
    target_link_libraries(sample_binaural PRIVATE maul-audio)
    maul_apply_flags(sample_binaural)
    if(NOT WIN32)
        target_link_libraries(sample_binaural PRIVATE m)
    endif()
endif()
