################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/System/Crc16.c 

OBJS += \
./App/System/Crc16.o 

C_DEPS += \
./App/System/Crc16.d 


# Each subdirectory must supply rules for building sources it contributes
App/System/%.o App/System/%.su App/System/%.cyclo: ../App/System/%.c App/System/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUNIT_TEST_ON_TARGET -DUNITY_INCLUDE_CONFIG_H -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I../App/Test -I../App/Test/Unity -I../App/Test/Suites -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/Comm/Inc" -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/System/Inc" -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/Hal_Wrapper/Inc" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-System

clean-App-2f-System:
	-$(RM) ./App/System/Crc16.cyclo ./App/System/Crc16.d ./App/System/Crc16.o ./App/System/Crc16.su

.PHONY: clean-App-2f-System

