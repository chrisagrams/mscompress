file(GLOB YXML_SOURCES ${VENDOR_DIR}/yxml/*.c)
add_library(yxml STATIC ${YXML_SOURCES})
set_target_properties(yxml PROPERTIES LINKER_LANGUAGE C)
