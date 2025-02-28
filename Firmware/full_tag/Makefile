all : flash

TARGET:=full_tag

ADDITIONAL_C_FILES += lib_i2c.c


TARGET_MCU?=CH32V003
include ../ch32v003fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean

