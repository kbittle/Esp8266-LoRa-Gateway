PROJECT_NAME := esp-gateway-project

# Get the short Git commit hash (fallback to 'nogit' if git isn't available)
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "nogit")

# Append the git hash to the output binary filename
# Output format: esp-gateway-project-a1b2c3d.bin
APP_OFFSET_NAME := $(PROJECT_NAME)-$(GIT_HASH)

include $(IDF_PATH)/make/project.mk
