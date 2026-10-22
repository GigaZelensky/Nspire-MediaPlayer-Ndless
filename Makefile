.DEFAULT_GOAL := all
DEBUG = FALSE
PROFILE ?= full
SUPPORTED_CODECS = h264 mpeg4 hevc av1
SUPPORTED_AUDIO_CODECS = pcm ima mp3 flac vorbis aac opus
ifeq ($(filter $(PROFILE),full lite custom),)
$(error PROFILE must be full, lite or custom)
endif
ifeq ($(PROFILE),lite)
CODECS ?= h264
AUDIO_CODECS ?=
USB_STORAGE ?= 0
else
CODECS ?= h264 mpeg4 hevc av1
AUDIO_CODECS ?= pcm ima mp3 flac vorbis aac opus
USB_STORAGE ?= 1
endif
ifneq ($(strip $(filter-out $(SUPPORTED_CODECS),$(CODECS))),)
$(error Unknown video codec in CODECS="$(CODECS)")
endif
ifneq ($(strip $(filter-out $(SUPPORTED_AUDIO_CODECS),$(AUDIO_CODECS))),)
$(error Unknown audio codec in AUDIO_CODECS="$(AUDIO_CODECS)")
endif
SELECTED_CODECS := $(strip $(foreach codec,$(SUPPORTED_CODECS),$(if $(filter $(codec),$(CODECS)),$(codec))))
SELECTED_AUDIO := $(strip $(foreach codec,$(SUPPORTED_AUDIO_CODECS),$(if $(filter $(codec),$(AUDIO_CODECS)),$(codec))))
ifeq ($(strip $(SELECTED_CODECS) $(SELECTED_AUDIO)),)
$(error Select at least one video or audio codec)
endif
ARTWORK ?= $(if $(SELECTED_AUDIO),1,0)
VISUALIZATIONS ?= $(if $(SELECTED_AUDIO),1,0)
CODEC_MODULES ?= $(if $(word 2,$(SELECTED_CODECS)),1,0)
AUDIO_MODULES ?= 1
$(foreach option,USB_STORAGE ARTWORK VISUALIZATIONS CODEC_MODULES AUDIO_MODULES,$(if $(filter $($(option)),0 1),,$(error $(option) must be 0 or 1)))
ifeq ($(SELECTED_AUDIO),)
ifneq ($(filter 1,$(ARTWORK) $(VISUALIZATIONS)),)
$(error ARTWORK and VISUALIZATIONS require an audio codec)
endif
endif
empty :=
space := $(empty) $(empty)
CODEC_TAG := $(if $(SELECTED_CODECS),$(subst $(space),-,$(SELECTED_CODECS)),none)
AUDIO_TAG := $(if $(SELECTED_AUDIO),$(subst $(space),-,$(SELECTED_AUDIO)),none)
CONFIG_TAG := $(CODEC_TAG)-audio-$(AUDIO_TAG)-usb$(USB_STORAGE)-art$(ARTWORK)-vis$(VISUALIZATIONS)-vm$(CODEC_MODULES)-am$(AUDIO_MODULES)
BUILD_FLAVOR := custom
ifeq ($(CODEC_TAG)|$(AUDIO_TAG)|$(USB_STORAGE)|$(ARTWORK)|$(VISUALIZATIONS),h264-mpeg4-hevc-av1|pcm-ima-mp3-flac-vorbis-aac-opus|1|1|1)
BUILD_FLAVOR := full
endif
ifeq ($(CODEC_TAG)|$(AUDIO_TAG)|$(USB_STORAGE)|$(ARTWORK)|$(VISUALIZATIONS),h264|none|0|0|0)
BUILD_FLAVOR := lite
endif
CODEC_SUFFIX := $(if $(filter full,$(BUILD_FLAVOR)),,-$(BUILD_FLAVOR))
EFFECTIVE_AUDIO_MODULES := $(if $(and $(filter 1,$(AUDIO_MODULES)),$(filter vorbis aac opus,$(SELECTED_AUDIO))),1,0)
AUDIO_UPPER_pcm = PCM
AUDIO_UPPER_ima = IMA
AUDIO_UPPER_mp3 = MP3
AUDIO_UPPER_flac = FLAC
AUDIO_UPPER_vorbis = VORBIS
AUDIO_UPPER_aac = AAC
AUDIO_UPPER_opus = OPUS
AUDIO_CONFIG_FLAGS = -DNDMEDIA_AUDIO=$(if $(SELECTED_AUDIO),1,0) $(foreach codec,$(SUPPORTED_AUDIO_CODECS),-DNDMEDIA_WITH_$(AUDIO_UPPER_$(codec))=$(if $(filter $(codec),$(SELECTED_AUDIO)),1,0))
# Preserve native device-settling margins in production. The guarded shorter
# waits remain available to dedicated tests, rather than every USB device.
FEATURE_FLAGS = -DNDMEDIA_USB_FAST_ENUMERATION=0 $(AUDIO_CONFIG_FLAGS) -DNDMEDIA_USB_STORAGE=$(USB_STORAGE) -DNDMEDIA_ARTWORK=$(ARTWORK) -DNDMEDIA_VISUALIZATIONS=$(VISUALIZATIONS)
CODEC_FLAGS = -DNDVIDEO_WITH_H264=$(if $(filter h264,$(SELECTED_CODECS)),1,0) \
 -DNDVIDEO_WITH_MPEG4=$(if $(filter mpeg4,$(SELECTED_CODECS)),1,0) \
 -DNDVIDEO_WITH_HEVC=$(if $(filter hevc,$(SELECTED_CODECS)),1,0) \
 -DNDVIDEO_WITH_AV1=$(if $(filter av1,$(SELECTED_CODECS)),1,0) \
 -DNDVIDEO_CODEC_MODULES=$(CODEC_MODULES) -DNDVIDEO_AUDIO_MODULES=$(EFFECTIVE_AUDIO_MODULES) $(FEATURE_FLAGS)
OBJDIR = build/$(if $(filter TRUE,$(DEBUG)),debug,release)/$(CONFIG_TAG)
MODULEDIR = build/$(if $(filter TRUE,$(DEBUG)),debug,release)/codec-modules/$(AUDIO_TAG)
ifneq ($(wildcard ./external/Ndless-official/ndless-sdk/include/libndls.h),)
SDKROOT ?= ./external/Ndless-official/ndless-sdk
else
SDKROOT ?= ./external/Ndless/ndless-sdk
endif
PYTHON ?= python
PACKZEHN = $(PYTHON) tools/pack_zehn.py
RAW_GXX ?= arm-none-eabi-g++
RAW_GCC ?= arm-none-eabi-gcc
OBJCOPY ?= arm-none-eabi-objcopy
LOADER = $(SDKROOT)/tools/zehn_loader/zehn_loader.tns
LOADER_DIR = $(SDKROOT)/tools/zehn_loader
LOADER_ELF = $(LOADER_DIR)/zehn_loader.tns.elf

export PATH := $(abspath $(SDKROOT)/bin):$(PATH)

GCC = nspire-gcc
AS  = nspire-as
GXX = nspire-g++
LD  = nspire-gcc -nodefaultlibs

GCCFLAGS_BASE = -Wall -Wextra -Wno-unused-parameter -std=c99 -marm -mcpu=arm926ej-s -mtune=arm926ej-s -mfloat-abi=soft -ffunction-sections -fdata-sections -Isrc -Isrc/codecs/h264bsd -Isrc/codecs -Isrc/codecs/xvid -DARCH_IS_32BIT -DARCH_IS_ARM -DXVID_DECODER_ONLY -DNDVIDEO_XVID_RGB565_ONLY -DNDVIDEO_XVID_NO_POSTPROC
AUDIO_PUBLIC_FLAGS = -Isrc/audio/vendor/ogg/include -Isrc/audio/vendor/faad2/include -Isrc/audio/vendor/opus/include -Isrc/audio/vendor/opusfile/include
AUDIO_VENDOR_FLAGS = $(AUDIO_PUBLIC_FLAGS) $(if $(filter src/audio/vendor/opus/%,$(1)),-Isrc/audio/vendor/opus -Isrc/audio/vendor/opus/silk -Isrc/audio/vendor/opus/silk/fixed -Isrc/audio/vendor/opus/celt,$(if $(filter src/audio/vendor/faad2/%,$(1)),-Isrc/audio/vendor/faad2/libfaad,$(if $(filter src/audio/vendor/tremor/%,$(1)),-Isrc/audio/vendor/tremor)))
LDFLAGS = -Wl,--gc-sections -lSDL -lm -flto -O3
LDFLAGS += -Wl,--wrap=fopen,--wrap=fread,--wrap=fwrite,--wrap=fseek,--wrap=fclose,--wrap=fflush,--wrap=_open,--wrap=_read,--wrap=_write,--wrap=_lseek,--wrap=_close,--wrap=remove,--wrap=nuc_opendir,--wrap=nuc_readdir,--wrap=nuc_closedir
LOADER_GXXFLAGS = -g -Os -Wall -Wextra -march=armv5te -fPIE -std=c++11 -fno-rtti -fno-exceptions -Wl,-Tldscript -Wl,--gc-sections -nostdlib -nostartfiles -ffreestanding -I ../../include
PACKFLAGS = --name "nMedia Player" --author "GigaZelensky" --version 1 --ndless-min 45 --hww-support --uses-lcd-blit --no-support-32mb

ifeq ($(DEBUG),FALSE)
	GCCFLAGS = $(GCCFLAGS_BASE) -Os -flto
	FAST_GCCFLAGS = $(GCCFLAGS_BASE) -O3 -DNDEBUG -fno-strict-aliasing -fomit-frame-pointer -falign-functions=32 -falign-loops=32 -flto -funroll-loops
else
	GCCFLAGS = $(GCCFLAGS_BASE) -O0 -g
	FAST_GCCFLAGS = $(GCCFLAGS_BASE) -O0 -g -falign-functions=32 -falign-loops=32
endif

# Keep aggressive unrolling/alignment on decode, pixel, and frame-timing paths.
# Picker, resource setup, and history code benefit from a smaller instruction footprint.
HOT_PLAYER_SRCS = src/player/music_visual.c src/player/music_effects.c src/player/video_lookahead.c src/player/video_decoder.c src/player/codec_streaming.c src/player/render_primitives.c src/player/sha_accel.c \
	src/player/playback_ui.c src/player/playback_loop.c src/player/subtitles.c \
	src/player/input_timing_memory.c src/player/night_mode.c \
	src/player/movie_open_scan.c src/player/platform_debug.c
FAST_SRCS = src/codecs/av1/% src/codecs/av1_decoder.c src/codecs/hevc/% src/codecs/hevc_decoder.cpp src/codecs/h264bsd/% src/codecs/xvid/% src/codecs/mpeg4_xvid.c \
	src/crypto/bearssl/% src/movie/nve_crypto.c src/audio/% $(HOT_PLAYER_SRCS)

XVID_DECODER_SRCS = \
	src/codecs/xvid/xvid.c \
	src/codecs/xvid/decoder.c \
	src/codecs/xvid/bitstream/bitstream.c \
	src/codecs/xvid/bitstream/cbp.c \
	src/codecs/xvid/bitstream/mbcoding.c \
	src/codecs/xvid/dct/fdct.c \
	src/codecs/xvid/dct/idct.c \
	src/codecs/xvid/dct/simple_idct.c \
	src/codecs/xvid/image/arm/yv12_to_rgb565.c \
	src/codecs/xvid/image/colorspace.c \
	src/codecs/xvid/image/font.c \
	src/codecs/xvid/image/image.c \
	src/codecs/xvid/image/interpolate8x8.c \
	src/codecs/xvid/image/qpel.c \
	src/codecs/xvid/image/reduced.c \
	src/codecs/xvid/motion/estimation_common.c \
	src/codecs/xvid/motion/gmc.c \
	src/codecs/xvid/motion/motion_comp.c \
	src/codecs/xvid/motion/sad.c \
	src/codecs/xvid/prediction/mbprediction.c \
	src/codecs/xvid/quant/quant_h263.c \
	src/codecs/xvid/quant/quant_matrix.c \
	src/codecs/xvid/quant/quant_mpeg.c \
	src/codecs/xvid/utils/emms.c \
	src/codecs/xvid/utils/mbtransquant.c \
	src/codecs/xvid/utils/mem_align.c \
	src/codecs/xvid/utils/mem_transfer.c \
	src/codecs/xvid/utils/sram_tables.c \
	src/codecs/xvid/utils/timer.c

# GNU make's wildcard skips hidden directories, including nested worktrees.
source_files = $(foreach entry,$(wildcard $(1)*),$(call source_files,$(entry)/,$(2)) $(filter $(2),$(entry)))
SOURCES := $(filter-out src/codecs/xvid/% src/codecs/modules/% src/audio/modules/%,$(call source_files,src/,%.c %.cpp %.S))
ifeq ($(filter h264,$(SELECTED_CODECS)),)
SOURCES := $(filter-out src/codecs/h264bsd/%,$(SOURCES))
endif
ifeq ($(filter mpeg4,$(SELECTED_CODECS)),)
SOURCES := $(filter-out src/codecs/mpeg4_xvid.c,$(SOURCES))
else
SOURCES += $(XVID_DECODER_SRCS)
endif
ifeq ($(filter hevc,$(SELECTED_CODECS)),)
SOURCES := $(filter-out src/codecs/hevc/% src/codecs/hevc_decoder.cpp,$(SOURCES))
endif
ifeq ($(filter av1,$(SELECTED_CODECS)),)
SOURCES := $(filter-out src/codecs/av1/% src/codecs/av1_decoder.c,$(SOURCES))
endif
ifeq ($(CODEC_MODULES),1)
SOURCES := $(filter-out src/codecs/h264bsd/% src/codecs/xvid/% src/codecs/mpeg4_xvid.c \
    src/codecs/hevc/% src/codecs/hevc_decoder.cpp src/codecs/av1/% src/codecs/av1_decoder.c,$(SOURCES))
MODULE_PROXY_h264 = src/codecs/modules/h264_module_proxy.c
MODULE_PROXY_mpeg4 = src/codecs/modules/mpeg4_module_proxy.c
MODULE_PROXY_hevc = src/codecs/modules/hevc_proxy.c
MODULE_PROXY_av1 = src/codecs/modules/av1_proxy.c
SOURCES += $(foreach codec,$(SELECTED_CODECS),$(MODULE_PROXY_$(codec)))
FAST_SRCS += src/codecs/modules/%
endif
ifeq ($(EFFECTIVE_AUDIO_MODULES),1)
SOURCES := $(filter-out src/audio/vendor/tremor/% src/audio/vendor/ogg/% src/audio/vendor/opus/% src/audio/vendor/opusfile/% src/audio/vendor/faad2/% src/audio/extra_ogg.c src/audio/extra_aac.c,$(SOURCES))
endif
MODULE_CODECS = $(if $(filter 1,$(CODEC_MODULES)),$(SELECTED_CODECS)) $(if $(filter 1,$(EFFECTIVE_AUDIO_MODULES)),$(filter vorbis aac opus,$(SELECTED_AUDIO)))
ifneq ($(strip $(MODULE_CODECS)),)
MODULE_FILES = $(foreach codec,$(MODULE_CODECS),$(MODULEDIR)/$(codec).zehn)
MODULE_ARGS = $(foreach codec,$(MODULE_CODECS),--module $(codec)=$(MODULEDIR)/$(codec).zehn)
MODULE_MANIFEST = $(OBJDIR)/codec_module_manifest.h
MODULE_SOURCES := src/media_config.h $(call source_files,src/codecs/,%.c %.cpp %.S %.h) $(call source_files,src/audio/,%.c %.h %.inc %.dat)
CODEC_FLAGS += -I$(OBJDIR)
endif
# Compile exactly the selected audio backends. LTO removes unused adapter calls.
ifeq ($(SELECTED_AUDIO),)
SOURCES := $(filter-out src/audio/% src/player/music% src/player/cover_image.c,$(SOURCES))
SOURCES += src/audio/movie_audio.c src/audio/audio_disabled.c
else
SOURCES := $(filter-out src/audio/audio_disabled.c,$(SOURCES))
ifeq ($(filter mp3,$(SELECTED_AUDIO)),)
SOURCES := $(filter-out src/audio/mp3_fixed.c src/audio/vendor/libmad/%,$(SOURCES))
endif
ifeq ($(filter vorbis,$(SELECTED_AUDIO)),)
SOURCES := $(filter-out src/audio/vendor/tremor/%,$(SOURCES))
endif
ifeq ($(filter opus,$(SELECTED_AUDIO)),)
SOURCES := $(filter-out src/audio/vendor/opus/% src/audio/vendor/opusfile/%,$(SOURCES))
endif
ifeq ($(filter vorbis opus,$(SELECTED_AUDIO)),)
SOURCES := $(filter-out src/audio/vendor/ogg/% src/audio/extra_ogg.c,$(SOURCES))
endif
ifeq ($(filter aac,$(SELECTED_AUDIO)),)
SOURCES := $(filter-out src/audio/vendor/faad2/% src/audio/extra_aac.c,$(SOURCES))
endif
ifeq ($(ARTWORK),0)
SOURCES := $(filter-out src/player/cover_image.c,$(SOURCES))
endif
endif
OBJS = $(addprefix $(OBJDIR)/,$(addsuffix .o,$(basename $(SOURCES))))
EXE = nMedia
TNS = _$(EXE)$(CODEC_SUFFIX).tns
DISTDIR = dist
RELEASEDIR = release
ELF = $(OBJDIR)/$(EXE).elf
ZEHN = $(OBJDIR)/$(EXE).zehn
PLAYER = $(OBJDIR)/$(TNS)
LEGACY_OBJS = src/player.o
RELEASE_TARGETS = release-full release-lite

CONFIG_ARGS = --flavor "$(BUILD_FLAVOR)" --video "$(SELECTED_CODECS)" --audio "$(SELECTED_AUDIO)" --usb "$(USB_STORAGE)" --artwork "$(ARTWORK)" --visualizations "$(VISUALIZATIONS)" --video-modules "$(CODEC_MODULES)" --audio-modules "$(EFFECTIVE_AUDIO_MODULES)" --flags="$(CODEC_FLAGS)" --sources="$(SOURCES)"
.PHONY: all clean release build-config $(RELEASE_TARGETS)

build-config:
	@$(PYTHON) tools/build_config.py $(CONFIG_ARGS)

all: $(PLAYER)
	mkdir -p $(DISTDIR)
	cp "$(PLAYER)" "$(DISTDIR)/$(TNS)"
	cp "$(ELF)" "$(DISTDIR)/$(EXE)$(CODEC_SUFFIX).elf"
	cp "$(ZEHN)" "$(DISTDIR)/$(EXE)$(CODEC_SUFFIX).zehn"
	$(PYTHON) tools/build_config.py $(CONFIG_ARGS) --output "$(DISTDIR)/$(EXE)$(CODEC_SUFFIX)-build.json"
ifneq ($(strip $(MODULE_CODECS)),)
	mkdir -p $(DISTDIR)/modules
	cp $(foreach codec,$(MODULE_CODECS),"$(MODULEDIR)/$(codec).elf" "$(MODULEDIR)/$(codec).zehn") $(DISTDIR)/modules/
endif

$(OBJDIR)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(GCC) $(if $(filter src/codecs/av1/% src/codecs/av1_decoder.c,$<),$(AV1_GCCFLAGS),$(if $(filter $(FAST_SRCS),$<),$(FAST_GCCFLAGS),$(GCCFLAGS))) $(if $(filter src/codecs/av1/%_tmpl.c,$<),-DBITDEPTH=8) $(CODEC_FLAGS) $(call AUDIO_VENDOR_FLAGS,$<) $(if $(filter src/codecs/xvid/%,$<),-Wno-incompatible-pointer-types,-Werror=incompatible-pointer-types) -MMD -MP -c $< -o $@

AV1_GCCFLAGS = $(filter-out -std=c99,$(FAST_GCCFLAGS)) -std=c11 -DDAV1D_STATIC \
	-Isrc/codecs/av1/dav1d -Isrc/codecs/av1/dav1d/include

HEVC_GXXFLAGS = $(filter-out -std=c99,$(FAST_GCCFLAGS)) -std=c++11 -fno-exceptions -fno-rtti \
    -DLIBDE265_STATIC_BUILD -DHAVE_STDINT_H -DHAVE_ALLOCA_H -Isrc/codecs/hevc

$(OBJDIR)/%.o: %.cpp Makefile
	mkdir -p $(dir $@)
	$(GXX) $(HEVC_GXXFLAGS) $(CODEC_FLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/%.o: %.S Makefile
	mkdir -p $(dir $@)
	$(AS) $(CODEC_FLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS) $(if $(filter 0,$(CODEC_MODULES)),$(if $(filter hevc,$(SELECTED_CODECS)),-lstdc++))

$(LOADER):
	cd $(LOADER_DIR) && $(RAW_GXX) $(LOADER_GXXFLAGS) loader.cpp -o zehn_loader.tns.elf
	$(OBJCOPY) --set-section-flags .pad=alloc,load,contents -O binary $(LOADER_ELF) $(LOADER)

ifneq ($(strip $(MODULE_CODECS)),)
$(MODULEDIR)/%.zehn: $(MODULE_SOURCES) src/sram.h tools/build_codec_modules.py tools/codec_module.ld tools/pack_zehn.py Makefile
	$(PYTHON) tools/build_codec_modules.py --codecs $* --output-dir "$(MODULEDIR)" --sdk "$(SDKROOT)" --cc "$(RAW_GCC)" --cxx "$(RAW_GXX)" --flags="$(if $(filter vorbis aac opus,$*),$(filter-out -funroll-loops -falign-functions=32 -falign-loops=32,$(FAST_GCCFLAGS)),$(FAST_GCCFLAGS)) $(AUDIO_CONFIG_FLAGS)" --jobs 2

$(MODULE_MANIFEST): $(MODULE_FILES) tools/bundle_codec_modules.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/bundle_codec_modules.py --manifest "$@" $(MODULE_ARGS)

$(OBJS): $(MODULE_MANIFEST)

$(PLAYER): $(ELF) $(LOADER) $(MODULE_FILES) tools/pack_zehn.py tools/bundle_codec_modules.py
	$(PACKZEHN) --input $< --output "$(OBJDIR)/host.tns" --zehn-output "$(OBJDIR)/host.zehn" --loader $(LOADER) $(PACKFLAGS)
	$(PYTHON) tools/bundle_codec_modules.py --host "$(OBJDIR)/host.zehn" --output "$(ZEHN)" $(MODULE_ARGS)
	$(PYTHON) tools/bundle_codec_modules.py --host "$(OBJDIR)/host.tns" --output "$@" $(MODULE_ARGS)
else
$(PLAYER): $(ELF) $(LOADER) tools/pack_zehn.py
	$(PACKZEHN) --input $< --output "$@" --zehn-output $(ZEHN) --loader $(LOADER) $(PACKFLAGS)
endif

release-full:
	$(MAKE) PROFILE=full CODECS="h264 mpeg4 hevc av1" AUDIO_CODECS="pcm ima mp3 flac vorbis aac opus" USB_STORAGE=1 ARTWORK=1 VISUALIZATIONS=1 all

release-lite:
	$(MAKE) PROFILE=lite CODECS=h264 AUDIO_CODECS="" USB_STORAGE=0 ARTWORK=0 VISUALIZATIONS=0 all

release: $(RELEASE_TARGETS)
	$(PYTHON) tools/package_release.py --dist "$(DISTDIR)" --output "$(RELEASEDIR)" --notes build/release-notes.md

clean:
	rm -f $(OBJS) $(LEGACY_OBJS) $(PLAYER) $(ELF) $(ZEHN) "$(DISTDIR)/$(TNS)" $(DISTDIR)/$(EXE)$(CODEC_SUFFIX).elf $(DISTDIR)/$(EXE)$(CODEC_SUFFIX).zehn
	rm -f $(OBJS:.o=.d)
ifneq ($(strip $(MODULE_CODECS)),)
	rm -f "$(MODULE_MANIFEST)" "$(OBJDIR)/host.tns" "$(OBJDIR)/host.zehn"
endif

-include $(OBJS:.o=.d)
