#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := greenhouse
COMMIT := $(shell git rev-list --max-count=1 --abbrev-commit HEAD)
DIRTY := $(shell git status -s)

ifneq "$(DIRTY)" ""
$(warning tree is dirty, please commit changes before running this)
COMMIT := "<dirty>"
endif

BUILD := $(shell date +"%Y%m%d.%H%M%S")-$(COMMIT)
export BUILD

.PHONY:	tag
all:	all_binaries tag
all_binaries:


include $(IDF_PATH)/make/project.mk

tag:	.tag

.tag:	$(APP_BIN)
	echo "tagging after building $(APP_BIN)"
	git tag -m "successful build" $(BUILD)
	touch .tag