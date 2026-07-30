# Headless page renderer: compiles the real EPUB layout engine + GfxRenderer
# natively and renders book pages into a stubbed 48KB framebuffer, dumped as
# PGM images. No gtest — this is a tool target, driven by render_pages args.
#
# Everything below is the exact code that runs on device; only Logging, the
# HAL (display/storage/GPIO), and the Arduino/FreeRTOS surface are stubbed
# (see stubs/).

set(PIO_LIBDEPS "${REPO_ROOT}/.pio/libdeps/default")

set(HEADLESS_SOURCES
  render_pages.cpp

  # --- Epub engine ---
  ${REPO_ROOT}/lib/Epub/Epub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Section.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/BookMetadataCache.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/ImageBlock.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/Hyphenator.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LanguageRegistry.cpp
  ${REPO_ROOT}/lib/Epub/Epub/hyphenation/LiangHyphenation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContainerParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ContentOpfParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNavParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/TocNcxParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDecoderFactory.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngToFramebufferConverter.cpp

  # --- renderer + fonts ---
  ${REPO_ROOT}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/EpdFont/FontDecompressor.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontManager.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/EpdFont/FlashReaderFont.cpp

  # --- support libs ---
  ${REPO_ROOT}/lib/JpegToBmpConverter/JpegToBmpConverter.cpp
  ${REPO_ROOT}/lib/PngToBmpConverter/PngToBmpConverter.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/ZipFile/ZipFile.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
  uzlib_checksums.c

  # --- expat (same srcFilter as library.json) ---
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c

  # --- image decoders (bitbank2, portable C/C++) ---
  ${PIO_LIBDEPS}/JPEGDEC/src/JPEGDEC.cpp
  ${PIO_LIBDEPS}/PNGdec/src/PNGdec.cpp
  ${PIO_LIBDEPS}/PNGdec/src/adler32.c
  ${PIO_LIBDEPS}/PNGdec/src/crc32.c
  ${PIO_LIBDEPS}/PNGdec/src/infback.c
  ${PIO_LIBDEPS}/PNGdec/src/inffast.c
  ${PIO_LIBDEPS}/PNGdec/src/inflate.c
  ${PIO_LIBDEPS}/PNGdec/src/inftrees.c
  ${PIO_LIBDEPS}/PNGdec/src/zutil.c
)

add_executable(render_pages ${HEADLESS_SOURCES})

target_include_directories(render_pages BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs   # must win over lib/hal + lib/Logging
)
target_include_directories(render_pages PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/JpegToBmpConverter
  ${REPO_ROOT}/lib/PngToBmpConverter
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/ZipFile
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/miniz/third_party
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/expat
  ${PIO_LIBDEPS}/JPEGDEC/src
  ${PIO_LIBDEPS}/PNGdec/src
)

target_compile_definitions(render_pages PRIVATE
  EINK_DISPLAY_SINGLE_BUFFER_MODE=1
  XML_CONTEXT_BYTES=1024
  XML_GE=0
  MINIZ_NO_ZLIB_COMPATIBLE_NAMES=1
  LOG_LEVEL=1
)

# The engine is warning-clean under -Wall on device toolchains; vendored C
# (expat/miniz/zlib/bitbank2) is not ours to fix, so no -Werror here.
target_compile_options(render_pages PRIVATE -Wall)
