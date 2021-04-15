set(COMPONENT_ADD_INCLUDEDIRS .
                            include)

register_component()

target_link_libraries(${COMPONENT_TARGET} INTERFACE "-L ${CMAKE_CURRENT_SOURCE_DIR}/lib")
if(CONFIG_CUPID_BOARD_V2)
target_link_libraries(${COMPONENT_TARGET} INTERFACE bds_light_sdk_cupid)
else()
target_link_libraries(${COMPONENT_TARGET} INTERFACE bds_light_sdk)
endif()

