set(SDCARD_FONT_SOURCES
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

# stubs/ first so the host fakes for Logging.h and HalStorage.h win over the
# Arduino-dependent real headers in lib/.
set(SDCARD_FONT_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

add_executable(SdCardFontIoTest SdCardFontIoTest.cpp ${SDCARD_FONT_SOURCES})
target_include_directories(SdCardFontIoTest PRIVATE ${SDCARD_FONT_INCLUDES})
target_link_libraries(SdCardFontIoTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdCardFontIoTest)

# Opt-in: replays real book pages through a real .cpfont. Needs CPFONT_PATH and
# PAGE_TEXT_FILE in the environment; skips cleanly otherwise, so it is safe to
# leave wired into ctest.
add_executable(RealBookPageTurnTest RealBookPageTurnTest.cpp ${SDCARD_FONT_SOURCES})
target_include_directories(RealBookPageTurnTest PRIVATE ${SDCARD_FONT_INCLUDES})
target_link_libraries(RealBookPageTurnTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(RealBookPageTurnTest)

# Opt-in: exercises the flash-primary / SD-tail hybrid. Compiles the generated
# 1.6MB font header, so it is a slow target.
add_executable(FlashHybridTest FlashHybridTest.cpp ${SDCARD_FONT_SOURCES}
  ${REPO_ROOT}/lib/EpdFont/FlashReaderFont.cpp)
target_include_directories(FlashHybridTest PRIVATE ${SDCARD_FONT_INCLUDES})
target_link_libraries(FlashHybridTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FlashHybridTest)
