SHELL := /bin/bash
ROOT_DIR := $(abspath $(lastword $(MAKEFILE_LIST)/../../../))
COMMON_DIR := ${ROOT_DIR}/common
COMMON_SRC_DIR := ${ROOT_DIR}/common/src
LIBMQDMA_DIR := ${ROOT_DIR}/libmqdma
LIBMCMEM_DIR := ${ROOT_DIR}/libmcmem

#Format: <production>.<major>.<minor>.<build>
RELEASE:=
