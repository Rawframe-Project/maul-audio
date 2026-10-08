# In-tree samples: small programs built with the library, each with the
# part it uses.

if(MAUL_AUDIO_DEVICE)
    add_executable(sample_devices devices.c)
    target_link_libraries(sample_devices PRIVATE maul-audio)
    maul_apply_flags(sample_devices)
endif()

if(MAUL_AUDIO_SPATIAL)
    add_executable(sample_binaural binaural.c)
    target_link_libraries(sample_binaural PRIVATE maul-audio)
    maul_apply_flags(sample_binaural)
    if(NOT WIN32)
        target_link_libraries(sample_binaural PRIVATE m)
    endif()
endif()
