set(COMPONENT_SRCS  ./app_control.c
                    ./app_player_init.c
                    ./app_sys_tools.c
                    ./cupid_ui.c
                    ./du1906_ui.c
                    ./print_sys_state.c
                    )

set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_PRIV_REQUIRES  audio_sal esp-adf-libs audio_pipeline audio_stream audio_hal audio_board esp_peripherals bluetooth_service audio_player bds_light bds_private json mqtt spi_flash nvs_flash playlist audio_flash_tone display_service boards input_key_service bdsc_peripheral_bt wifi_service bdsc_dispatcher bdsc_ota bdsc_auth bdsc_utils bdsc_profile)
register_component()

component_compile_options(-Wno-unused-variable -Wno-enum-compare)
