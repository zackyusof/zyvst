set(ZYV_SOURCES
    Source/ChainEngine.cpp
    Source/ChainEngine.h
    Source/Engine.h
    Source/HybridEditor.cpp
    Source/HybridEditor.h
    Source/HybridProcessor.cpp
    Source/HybridProcessor.h
    Source/MidiClock.h
    Source/PluginEntry.cpp)

set(ZYV_JUCE_MODULES
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_audio_processors
    juce::juce_audio_processors_headless)

function(zyv_add_au target subtype au_type is_synth is_midi_effect product plugin_name bundle_id)
    juce_add_plugin(${target}
        COMPANY_NAME "ZYVS"
        COMPANY_COPYRIGHT "GPL-3.0 seed + MIT patterns; see NOTICE"
        BUNDLE_ID "${bundle_id}"
        PLUGIN_MANUFACTURER_CODE Zyvs
        PLUGIN_CODE ${subtype}
        PRODUCT_NAME "${product}"
        PLUGIN_NAME "${plugin_name}"
        DESCRIPTION "AU wrapper hosting a VST3 chain"
        FORMATS AU
        AU_MAIN_TYPE ${au_type}
        IS_SYNTH ${is_synth}
        IS_MIDI_EFFECT ${is_midi_effect}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        VERSION ${PROJECT_VERSION}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    juce_generate_juce_header(${target})

    target_sources(${target} PRIVATE ${ZYV_SOURCES})

    target_compile_definitions(${target} PRIVATE
        ZYV_BUILD_LABEL="${PROJECT_VERSION}"
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_DISABLE_ASSERTIONS=0)

    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Release>:-O3 -ffast-math -fno-math-errno -fno-trapping-math>)

    target_link_libraries(${target}
        PRIVATE ${ZYV_JUCE_MODULES}
        PUBLIC juce::juce_recommended_config_flags
               juce::juce_recommended_warning_flags)

    target_include_directories(${target} PRIVATE Source)

    # The linker signature alone does not seal the completed AU bundle when
    # building with Makefiles/Command Line Tools. Reuse JUCE's signing helper,
    # then check the final bundle, without installing or claiming notarization.
    add_custom_command(TARGET ${target}_AU POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-Dsrc=$<TARGET_BUNDLE_DIR:${target}_AU>"
            -P "${PROJECT_SOURCE_DIR}/third_party/JUCE/extras/Build/CMake/checkBundleSigning.cmake"
        COMMAND /usr/bin/codesign --verify --strict "$<TARGET_BUNDLE_DIR:${target}_AU>"
        VERBATIM)
endfunction()

if(ZYV_BUILD_FX)
    zyv_add_au(HybridFxA "ZyFx" kAudioUnitType_Effect FALSE FALSE "zyvst_fx" "zyvst_fx" "org.zyvst.fx")
endif()

if(ZYV_BUILD_INST)
    zyv_add_au(HybridInstA "ZyIn" kAudioUnitType_MusicDevice TRUE FALSE "zyvst_inst" "zyvst_inst" "org.zyvst.instrument")
endif()

if(ZYV_BUILD_MIDI)
    zyv_add_au(HybridMidiA "ZyMd" kAudioUnitType_MIDIProcessor FALSE TRUE "zyvst_midi" "zyvst_midi" "org.zyvst.midi")
endif()
