################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
LD_SRCS += \
../src/lscript.ld 

C_SRCS += \
../src/dc_byte_ring.c \
../src/dc_node.c \
../src/dc_protocol.c \
../src/dc_zynq_log.c \
../src/dc_zynq_port.c \
../src/main_hooks.c \
../src/standalone_main.c 

OBJS += \
./src/dc_byte_ring.o \
./src/dc_node.o \
./src/dc_protocol.o \
./src/dc_zynq_log.o \
./src/dc_zynq_port.o \
./src/main_hooks.o \
./src/standalone_main.o 

C_DEPS += \
./src/dc_byte_ring.d \
./src/dc_node.d \
./src/dc_protocol.d \
./src/dc_zynq_log.d \
./src/dc_zynq_port.d \
./src/main_hooks.d \
./src/standalone_main.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: ARM v7 gcc compiler'
	arm-none-eabi-gcc -Wall -O0 -g3 -c -fmessage-length=0 -MT"$@" -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard -std=c99 -Wall -Wextra -Werror -DDC_UART_BAUD=460800u -DDC_ZYNQ_BOARD_READY=1 -I../../dc_bsp/ps7_cortexa9_0/include -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


