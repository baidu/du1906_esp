set(COMPONENT_SRCS  ./app_console.c)

set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_PRIV_REQUIRES  audio_sal esp-adf-libs audio_pipeline audio_stream audio_hal audio_board esp_peripherals bluetooth_service audio_player bds_light bds_private json mqtt spi_flash nvs_flash playlist audio_flash_tone display_service bdsc_auth bdsc_dispatcher bdsc_dispatcher bdsc_utils bdsc_profile)

register_component()
