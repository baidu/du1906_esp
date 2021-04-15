#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)


COMPONENT_ADD_INCLUDEDIRS := include migu_sdk/include/
COMPONENT_SRCDIRS := . migu_sdk/.
COMPONENT_ADD_LDFLAGS += -L$(COMPONENT_PATH)/migu_sdk/lib -lmigu -lmigu_https -lmigu_music