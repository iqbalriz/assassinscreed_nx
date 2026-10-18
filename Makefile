#---------------------------------------------------------------------------------
# Assassin's Creed: Altair's Chronicles HD -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the game's own APK (the user's copy,
# any file name) is read at run time; its libraries are unpacked from it on
# the first launch, its assets read from it at every start (source/ro_assets.c),
# and its data folder is the player's own.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh (or build.ps1 on Windows) runs
# it in the toolchain container. Output: assassinscreed_nx.nsp, which the launcher NRO
# carries (launcher/).
#---------------------------------------------------------------------------------
TARGET               := assassinscreed_nx
PORT_NPDM_PROGRAM_ID := 0x010071B8D4CCC704
PORT_NPDM_VERSION    := 0.1.1
PORT_NPDM_MAIN_STACK := 0x400000
# The intro movie: FFmpeg's MP4 demuxer and MPEG-4 / AAC decoders, built by
# ffmpeg32's build.sh (LGPL) and copied into portlibs32/. Without them
# ac_video.c builds without pictures (the game skips its intro).
DCR_VIDEO := $(if $(wildcard portlibs32/lib/libavcodec.a),1,0)
ifeq ($(DCR_VIDEO),1)
PORT_LIBS := -L$(CURDIR)/portlibs32/lib -lavformat -lavcodec -lavutil
endif
include runtime/runtime.mk

# FFmpeg is built with int-sized enums (-fno-short-enums), and so is its one
# user here, whose own interface has no enums.
$(BUILD)/ac_video.o: $(SOURCES)/ac_video.c $(RENDERER_STAMP) | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -fno-short-enums -DDCR_VIDEO=$(DCR_VIDEO) -c $< -o $@
