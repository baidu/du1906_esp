set(COMPONENT_SRCS  ./app_ota_upgrade.c
                    ./ota_proc_iot.c
                    )

set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_PRIV_REQUIRES  audio_sal esp-adf-libs audio_pipeline audio_stream audio_hal audio_board esp_peripherals bluetooth_service audio_player bds_light bds_private json mqtt spi_flash nvs_flash playlist audio_flash_tone display_service bdsc_auth bdsc_utils bdsc_profile bdsc_dispatcher esp_https_ota ota_service bdsc_json boards)

register_component()
