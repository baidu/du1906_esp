#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_SRCDIRS := .
COMPONENT_ADD_INCLUDEDIRS := include 

CFLAGS += -D APP_VER=\"$(shell git rev-parse HEAD)\"

# These tone are not updatable.
# We must keep these tone playing ok.
COMPONENT_EMBED_TXTFILES := \
../../tone/downloaded.mp3 \
../../tone/ota_fail.mp3 \
../../tone/ota_start.mp3 \
../../tone/already_new.mp3 \
../../tone/dsp_load_fail.mp3 \
../../tone/bad_net_report.mp3
