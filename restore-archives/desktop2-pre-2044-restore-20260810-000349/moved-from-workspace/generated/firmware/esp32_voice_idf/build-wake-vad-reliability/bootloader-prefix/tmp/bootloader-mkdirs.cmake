# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/mac/.espressif/frameworks/esp-idf-v5.5.4/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/Users/mac/.espressif/frameworks/esp-idf-v5.5.4/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader"
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix"
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/tmp"
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/src/bootloader-stamp"
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/src"
  "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/mac/Desktop/2/firmware/esp32_voice_idf/build-wake-vad-reliability/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
