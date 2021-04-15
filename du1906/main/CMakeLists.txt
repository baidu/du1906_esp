set(COMPONENT_SRCS  main.c
                    )


set(COMPONENT_ADD_INCLUDEDIRS . )


set(COMPONENT_REQUIRES 
        audio_sal
        audio_hal
        esp_dispatcher
        esp_peripherals
        display_service
        input_key_service
        bluetooth_service
        wifi_service
        audio_player
        audio_flash_tone
        bdsc_dispatcher
        bdsc_mqtt
        app_init
        bds_light
        ota_service
        mqtt
        bdsc_auth
        bdsc_utils
        bdsc_profile
        json
        bdsc_json
        bdsc_dm
        bdsc_vc
        )

register_component()
