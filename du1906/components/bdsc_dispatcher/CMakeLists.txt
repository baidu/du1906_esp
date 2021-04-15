set(COMPONENT_SRCS  ./bdsc_cmd.c
                    ./bdsc_engine.c
                    ./bdsc_event_dispatcher.c
                    ./raw_play_task.c
                    )

set(COMPONENT_ADD_INCLUDEDIRS include)
set(COMPONENT_PRIV_REQUIRES  audio_sal esp-adf-libs audio_pipeline audio_stream audio_hal audio_board esp_peripherals bluetooth_service audio_player bds_light bds_private json mqtt spi_flash nvs_flash playlist audio_flash_tone display_service bdsc_auth bdsc_utils bdsc_profile wifi_service bdsc_json)

register_component()

component_compile_options(-Wno-unused-variable -Wno-enum-compare)

component_compile_options("-DAPP_VER=\"$(shell git rev-parse HEAD)\"")
