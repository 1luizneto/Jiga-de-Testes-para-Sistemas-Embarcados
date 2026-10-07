################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../App/Hal_Wrapper/Bsp.c \
../App/Hal_Wrapper/Bsp_Crc.c \
../App/Hal_Wrapper/Bsp_Uart.c \
../App/Hal_Wrapper/Bsp_Usb.c 

OBJS += \
./App/Hal_Wrapper/Bsp.o \
./App/Hal_Wrapper/Bsp_Crc.o \
./App/Hal_Wrapper/Bsp_Uart.o \
./App/Hal_Wrapper/Bsp_Usb.o 

C_DEPS += \
./App/Hal_Wrapper/Bsp.d \
./App/Hal_Wrapper/Bsp_Crc.d \
./App/Hal_Wrapper/Bsp_Uart.d \
./App/Hal_Wrapper/Bsp_Usb.d 


# Each subdirectory must supply rules for building sources it contributes
App/Hal_Wrapper/%.o App/Hal_Wrapper/%.su App/Hal_Wrapper/%.cyclo: ../App/Hal_Wrapper/%.c App/Hal_Wrapper/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F767xx -c -I../Core/Inc -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/Comm/Inc" -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/System/Inc" -I"C:/Users/LuizOliveiradeSouzaN/Documents/TCC/Firmware/Jiga-de-Testes-para-Sistemas-Embarcados/projeto/App/Hal_Wrapper/Inc" -I../Drivers/STM32F7xx_HAL_Driver/Inc -I../Drivers/STM32F7xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM7/r0p1 -I../Drivers/CMSIS/Device/ST/STM32F7xx/Include -I../Drivers/CMSIS/Include -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-App-2f-Hal_Wrapper

clean-App-2f-Hal_Wrapper:
	-$(RM) ./App/Hal_Wrapper/Bsp.cyclo ./App/Hal_Wrapper/Bsp.d ./App/Hal_Wrapper/Bsp.o ./App/Hal_Wrapper/Bsp.su ./App/Hal_Wrapper/Bsp_Crc.cyclo ./App/Hal_Wrapper/Bsp_Crc.d ./App/Hal_Wrapper/Bsp_Crc.o ./App/Hal_Wrapper/Bsp_Crc.su ./App/Hal_Wrapper/Bsp_Uart.cyclo ./App/Hal_Wrapper/Bsp_Uart.d ./App/Hal_Wrapper/Bsp_Uart.o ./App/Hal_Wrapper/Bsp_Uart.su ./App/Hal_Wrapper/Bsp_Usb.cyclo ./App/Hal_Wrapper/Bsp_Usb.d ./App/Hal_Wrapper/Bsp_Usb.o ./App/Hal_Wrapper/Bsp_Usb.su

.PHONY: clean-App-2f-Hal_Wrapper

