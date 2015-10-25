#   OPTIMIZER	Override the optimization level (optional, defaults to -O2)
#   INCDIRS	Specify any additional include directories.
#   LIBDIRS	Specify any addition library include directories
#   SOURCES	List all .C files required to build the DSO or App
#   DSONAME	Name of the desired output DSO file (if applicable)
#   APPNAME	Name of the desires output application (if applicable)
#   INSTDIR	Directory to be installed. If not specified, this will
#		default to the the HOME houdini directory.
#   ICONS	Name of the icon files to install (optionial)

HFS = @HOUDINI_ROOT@
INCDIRS = -std=c++11 -I@PRT_INCLUDE_PATH@ -I@CMAKE_SOURCE_DIR@
LIBDIRS = -L@PRT_INSTALL_PATH@/bin -lcom.esri.prt.core -Wl,-rpath,@PRT_INSTALL_PATH@/bin

DSONAME = SOP_prt4houdini.so 
SOURCES = @CMAKE_CURRENT_BINARY_DIR@/SOP_prt4houdini.cpp @CMAKE_CURRENT_BINARY_DIR@/utils.cpp

#export PATH := @houdini_DIR@/bin:$(PATH)
include ${HFS}/toolkit/makefiles/Makefile.gnu