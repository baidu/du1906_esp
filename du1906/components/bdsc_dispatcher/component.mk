#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_SRCDIRS := .
COMPONENT_ADD_INCLUDEDIRS := include 

CFLAGS += -D APP_VER=\"$(shell git rev-parse HEAD)\"

