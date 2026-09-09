# Include SWSD003 header paths
COMPONENT_ADD_INCLUDEDIRS := . \
                             ../SWSD003/sx126x/sx126x_driver/src \
                             ../SWSD003/common/inc

# Include SWSD003 driver C files into compilation
COMPONENT_SRCDIRS := . \
                     ../SWSD003/sx126x/sx126x_driver/src

COMPONENT_EMBED_TXTFILES := index.html