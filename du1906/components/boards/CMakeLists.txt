# Edit following two lines to set component requirements (see docs)
set(COMPONENT_PRIV_REQUIRES audio_sal audio_hal esp_dispatcher esp_peripherals display_service bdsc_dispatcher bdsc_auth)


if(CONFIG_CUPID_BOARD_V2)
message(STATUS "Current board name is " CONFIG_CUPID_BOARD_V2)
set(COMPONENT_ADD_INCLUDEDIRS ./cupid/include)
set(COMPONENT_SRCS
./cupid/board.c
./cupid/board_pins_config.c
./cupid/es7243e_driver.c
)
endif()

set(COMPONENT_REQUIRES audio_sal audio_hal esp_dispatcher esp_peripherals display_service)

IF (IDF_VER MATCHES "v4.")
idf_component_get_property(audio_board_lib audio_board COMPONENT_LIB)
set_property(TARGET ${audio_board_lib} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${COMPONENT_LIB})

ELSEIF (IDF_VER MATCHES "v3.")
set_property(TARGET idf_component_audio_board APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES  $<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)

ENDIF (IDF_VER MATCHES "v4.")

register_component()

