SHELL := /bin/bash
ROOT_DIR := $(abspath $(lastword $(MAKEFILE_LIST)/../../../))
COMMON_DIR := ${ROOT_DIR}/common
COMMON_SRC_DIR := ${ROOT_DIR}/common/src
DRIVER_DIR := ${ROOT_DIR}/driver

#Format: <production>.<major>.<minor>.<build>
RELEASE:=
