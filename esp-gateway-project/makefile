# Get the short Git commit hash (fallback to 'nogit' if git isn't available)
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "nogit")

# Dynamic project name incorporating the hash
PROJECT_NAME := esp-gateway-project-$(GIT_HASH)

include $(IDF_PATH)/make/project.mk
