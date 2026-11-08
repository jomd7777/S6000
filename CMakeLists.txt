# AKAI S6000 style sampler - OS Ver. 2.14 emulation  (c) dominikHop
# Alternative to the Projucer project:
#   cmake -B build -DJUCE_DIR=/path/to/JUCE  [-DAAX_SDK=/path/to/aax-sdk]
#   cmake --build build --config Release
cmake_minimum_required (VERSION 3.22)
project (S6000 VERSION 2.14.1)

set (CMAKE_CXX_STANDARD 17)
set (JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE 8")
add_subdirectory (${JUCE_DIR} JUCE)

set (S6000_FORMATS AU VST3 Standalone)
if (AAX_SDK)
    juce_set_aax_sdk_path (${AAX_SDK})
    list (APPEND S6000_FORMATS AAX)
endif()

juce_add_plugin (S6000
    COMPANY_NAME "dominikHop"
    COMPANY_COPYRIGHT "(c) dominikHop"
    COMPANY_WEBSITE "https://www.instagram.com/dominikhop"
    BUNDLE_ID com.dominikhop.s6000
    PRODUCT_NAME "S6000"
    DESCRIPTION "AKAI S6000 style stereo digital sampler - OS 2.14"
    PLUGIN_MANUFACTURER_CODE Dmhp
    PLUGIN_CODE S6k0
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "S6000 RECORD mode records from the audio input."
    VST3_CATEGORIES Instrument Sampler
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AAX_CATEGORY AAX_ePlugInCategory_SWGenerators
    FORMATS ${S6000_FORMATS})

juce_generate_juce_header (S6000)

target_sources (S6000 PRIVATE
    Source/Core/DiskManager.cpp
    Source/Core/EffectsEngine.cpp
    Source/Core/Model.cpp
    Source/Core/SampleOps.cpp
    Source/Core/SamplerEngine.cpp
    Source/Core/VoiceFilter.cpp
    Source/Disk/AkaiFs.cpp
    Source/Disk/DiskDevice.cpp
    Source/Disk/DmgReader.cpp
    Source/Disk/FatFs.cpp
    Source/Disk/Iso9660.cpp
    Source/Formats/AkaiFormats.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/UI/Controls.cpp
    Source/UI/PageSystem.cpp
    Source/UI/Pages/FxPages.cpp
    Source/UI/Pages/LoadPages.cpp
    Source/UI/Pages/MultiPages.cpp
    Source/UI/Pages/ProgramPages.cpp
    Source/UI/Pages/RecordPage.cpp
    Source/UI/Pages/SamplePages.cpp
    Source/UI/Pages/SavePage.cpp
    Source/UI/Pages/UtilityPages.cpp
    Source/UI/Popups.cpp
    Source/UI/WaveView.cpp)

target_compile_definitions (S6000 PUBLIC
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries (S6000
    PRIVATE
        juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
        juce::juce_audio_plugin_client juce::juce_audio_processors juce::juce_audio_utils
        juce::juce_core juce::juce_data_structures juce::juce_events
        juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---- standalone tests of the disk / format layer (no JUCE needed) --------------
option (S6000_BUILD_TESTS "Build the disk image tests" OFF)
if (S6000_BUILD_TESTS)
    find_package (ZLIB REQUIRED)
    add_executable (s6000_disk_test Tests/disk_test.cpp
        Source/Disk/DiskDevice.cpp Source/Disk/Iso9660.cpp Source/Disk/FatFs.cpp
        Source/Disk/AkaiFs.cpp Source/Disk/DmgReader.cpp Source/Formats/AkaiFormats.cpp)
    target_link_libraries (s6000_disk_test PRIVATE ZLIB::ZLIB)
endif()
