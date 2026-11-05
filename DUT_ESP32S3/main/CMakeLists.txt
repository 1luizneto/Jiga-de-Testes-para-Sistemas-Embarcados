idf_component_register(SRCS "main.c" "comandos.c" "mapa_i2c.c" "barramentos.c" "ventilador.c" "drivers_sensores.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_uart esp_driver_i2c esp_driver_spi esp_driver_gpio esp_adc)

# Falha injetada no ventilador do CT22 (0 = firmware correto): idf.py -DVENTILADOR_DEFEITO=n build flash
if(DEFINED VENTILADOR_DEFEITO)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE dVENTILADOR_DEFEITO=${VENTILADOR_DEFEITO})
endif()
