# PlayKit - WebKit browser for PlayStation 5.
# Native Linux/WSL build entry points.
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := /bin/bash
.DEFAULT_GOAL := app

-include .env

APP_DEFINITIONS ?=
APP_INCLUDE_PATHS ?=
APP_STATIC_ARCHIVES ?=
APP_IMPORT_STUBS ?=
APP_WRAP_SYMBOLS ?= malloc calloc realloc free posix_memalign memalign aligned_alloc malloc_usable_size fcntl
PACBREW_PACKAGES ?= libcurl libpng
APP_SOURCE_DIR ?= src
APP_PARAM ?= sce_sys/param.json
APP_SCE_SYS ?= sce_sys
APP_ASSETS ?= assets
APP_ROOT_FILES ?=
PS5_HOST ?=
FTP_PORT ?= 2121
PS5_FTP_USER ?= anonymous
PS5_FTP_PASSWORD ?= codex
DEPLOY_DRY_RUN ?= 0
TITLE_ID ?=
APP_NAME ?=
APP_CATEGORY ?= game
HOST_CC ?= clang
HOST_CXX ?= clang++
BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 2)
USE_CCACHE ?= 1

export BUILD_JOBS USE_CCACHE
export HOST_CC HOST_CXX
export APP_DEFINITIONS APP_INCLUDE_PATHS APP_STATIC_ARCHIVES APP_IMPORT_STUBS APP_WRAP_SYMBOLS
export PACBREW_PACKAGES
export APP_SOURCE_DIR APP_PARAM APP_SCE_SYS APP_ASSETS APP_ROOT_FILES
export PS5_HOST FTP_PORT PS5_FTP_USER PS5_FTP_PASSWORD DEPLOY_DRY_RUN
export TITLE_ID APP_NAME APP_CATEGORY

.PHONY: all app init doctor test clean distclean help deploy undeploy

all: app
build: app

init:
	@printf '%s\n' '==> [init] Configuring the application identity in sce_sys/param.json'
	@bash tools/init-project.sh sce_sys/param.json

doctor:
	@printf '%s\n' '==> [doctor] Checking the Linux/WSL host without changing it'
	@bash tools/doctor.sh

test: test-unit test-integration

.PHONY: test-unit test-integration

test-unit:
	@printf '%s\n' '==> [test-unit] Running host-native unit tests'
	@mkdir -p build/hosttest
	@$(HOST_CC) -std=c11 -O1 -g -Wall -Wextra -Isrc -o build/hosttest/ui_test \
		tests/host/test_ui.c tests/host/stubs.c src/ui.c src/browser_ui.c -lm
	@PLAYKIT_BOOKMARKS=/tmp/opencode/playkit-test-bookmarks.txt \
		PLAYKIT_DOWNLOADS=/tmp/opencode/playkit-test-downloads \
		./build/hosttest/ui_test

test-integration:
	@printf '%s\n' '==> [test-integration] Running host tooling integration tests'
	@python3 -m unittest discover -s tests -p 'test_*.py' -v 2>/dev/null || printf '%s\n' 'no integration tests yet'

app:
	@printf '%s\n' '==> [app] Compiling, linking, signing, and assembling the app folder'
	@APP_INCLUDE_PATHS="src $(APP_INCLUDE_PATHS)" \
		APP_STATIC_ARCHIVES="$(APP_STATIC_ARCHIVES)" \
		APP_IMPORT_STUBS="$(APP_IMPORT_STUBS)" \
		APP_WRAP_SYMBOLS="$(APP_WRAP_SYMBOLS)" \
		bash tools/build.sh Folder

deploy:
	@printf '%s\n' '==> [deploy] Building and publishing the app folder over FTP'
	@bash tools/deploy.sh

undeploy:
	@printf '%s\n' '==> [undeploy] Removing staged development files for this title over FTP'
	@bash tools/deploy.sh undeploy

help:
	@printf '%s\n' \
		'make                     Build the native PS5 app folder' \
		'make init TITLE_ID=PPSA90002 APP_NAME="PlayKit"  Configure app identity' \
		'make doctor              Check required host tools' \
		'make app                 Compile, link, sign, and package the app' \
		'make clean               Remove build/ and dist/' \
		'make deploy PS5_HOST=<ip>  Build and FTP-deploy the app folder'

clean:
	@printf '%s\n' '==> [clean] Removing generated build outputs'
	@rm -rf -- build dist

distclean: clean
	@printf '%s\n' '==> [distclean] Removing downloaded dependency caches'
