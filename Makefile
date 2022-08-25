CC = gcc

CFLAGS += -O3 -Wall -Wextra -pedantic -msse4.2
DEBUG_CFLAGS += -g

OBJS = \
	vendor/yxml/yxml.o \
	vendor/base64/lib/arch/avx2/codec.o \
	vendor/base64/lib/arch/generic/codec.o \
	vendor/base64/lib/arch/neon32/codec.o \
	vendor/base64/lib/arch/neon64/codec.o \
	vendor/base64/lib/arch/ssse3/codec.o \
	vendor/base64/lib/arch/sse41/codec.o \
	vendor/base64/lib/arch/sse42/codec.o \
	vendor/base64/lib/arch/avx/codec.o \
	vendor/base64/lib/lib.o \
	vendor/base64/lib/codec_choose.o \
	vendor/base64/lib/tables/tables.o \
	src/queue.o \
	src/sys.o \
	src/preprocess.o \
	src/file.o \
	src/encode.o \
	src/decode.o \
	src/compress.o \
	src/decompress.o \
	src/zl.o \
	src/debug.o \
	src/mscompress.o

LIBS				:=
ifeq ($(OS),Windows_NT)
    UNAME_S := Windows
else
    UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		LIBS += -largp -lm -lzstd -lpthread -I./src -I./
	endif
	ifeq ($(UNAME_S),Linux)
		LIBS += -lm -lzstd -lpthread -I./src -I./ -L./vendor/zlib/ -l:libz.so
	endif
endif

HAVE_AVX2   = 0
HAVE_NEON32 = 0
HAVE_NEON64 = 0
HAVE_SSSE3  = 0
HAVE_SSE41  = 0
HAVE_SSE42  = 0
HAVE_AVX    = 0

# The user should supply compiler flags for the codecs they want to build.
# Check which codecs we're going to include:
ifdef AVX2_CFLAGS
  HAVE_AVX2 = 1
endif
ifdef NEON32_CFLAGS
  HAVE_NEON32 = 1
endif
ifdef NEON64_CFLAGS
  HAVE_NEON64 = 1
endif
ifdef SSSE3_CFLAGS
  HAVE_SSSE3 = 1
endif
ifdef SSE41_CFLAGS
  HAVE_SSE41 = 1
endif
ifdef SSE42_CFLAGS
  HAVE_SSE42 = 1
endif
ifdef AVX_CFLAGS
  HAVE_AVX = 1
endif
ifdef OPENMP
  CFLAGS += -fopenmp
endif

mscompress: $(OBJS)
	@echo $(LIBS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

ifeq ($(OS),Windows_NT)
clean:
	del $(OBJS) vendor/base64/lib/config.h ./mscompress
else
clean:
	rm $(OBJS) vendor/base64/lib/config.h ./mscompress ||:
endif

debug: CFLAGS = $(DEBUG_CFLAGS)

debug: $(OBJS)
	$(CC) $(CFLAGS) -o mscompress $^ $(LIBS)

vendor/base64/lib/config.h:
	@echo "#define HAVE_AVX2   $(HAVE_AVX2)"    > $@
	@echo "#define HAVE_NEON32 $(HAVE_NEON32)" >> $@
	@echo "#define HAVE_NEON64 $(HAVE_NEON64)" >> $@
	@echo "#define HAVE_SSSE3  $(HAVE_SSSE3)"  >> $@
	@echo "#define HAVE_SSE41  $(HAVE_SSE41)"  >> $@
	@echo "#define HAVE_SSE42  $(HAVE_SSE42)"  >> $@
	@echo "#define HAVE_AVX    $(HAVE_AVX)"    >> $@

$(OBJS): vendor/base64/lib/config.h vendor/zlib/zlib.h 

vendor/base64/ib/arch/avx2/codec.o:   CFLAGS += $(AVX2_CFLAGS)
vendor/base64/lib/arch/neon32/codec.o: CFLAGS += $(NEON32_CFLAGS)
vendor/base64/lib/arch/neon64/codec.o: CFLAGS += $(NEON64_CFLAGS)
vendor/base64/lib/arch/ssse3/codec.o:  CFLAGS += $(SSSE3_CFLAGS)
vendor/base64/lib/arch/sse41/codec.o:  CFLAGS += $(SSE41_CFLAGS)
vendor/base64/lib/arch/sse42/codec.o:  CFLAGS += $(SSE42_CFLAGS)
vendor/base64/lib/arch/avx/codec.o:    CFLAGS += $(AVX_CFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $< $(LIBS)


