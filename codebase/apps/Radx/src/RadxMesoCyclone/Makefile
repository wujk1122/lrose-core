###########################################################################
#
# Makefile for RadxMesoCyclone
#
###########################################################################

include $(LROSE_CORE_DIR)/build/make_include/lrose_make_macros

TARGET_FILE=RadxMesoCyclone
LOC_INCLUDES = -I. $(NETCDF4_INCS)

LOC_LIBS = -lFiltAlgVirtVol -lradar -lphysics \
	-ldsdata -lSpdb -lMdv -lRadx -lNcxx \
	-ldsserver -ldidss -lrapformats -leuclid \
	-lrapmath -ltoolsa -ldataport -ltdrp \
	$(NETCDF4_LIBS) -lbz2 -lz -lpthread \
	-lm

LOC_LDFLAGS = $(NETCDF4_LDFLAGS)

LOC_INC_DIR = .
MY_CPPCHECKFLAGS = -E Params.cc -E Params.hh -exclude-rules=4.16
LOC_CFLAGS = 
LOC_CPPC_CFLAGS = 

HDRS = 

CPPC_SRCS = \
	Params.cc \
	Parms.cc \
	Main.cc \
	MesoTemplate.cc \
	RadxMesoCyclone.cc \
	Sweep.cc \
	TemplateLookup.cc \
	TemplateLookup1.cc \
	TemplateLookupMgr.cc \
	Volume.cc
#
# tdrp macros
#

include $(LROSE_CORE_DIR)/build/make_include/lrose_make_tdrp_macros

#
# C++ targets
#

include $(LROSE_CORE_DIR)/build/make_include/lrose_make_c++_targets

#
# tdrp targets
#

include $(LROSE_CORE_DIR)/build/make_include/lrose_make_tdrp_c++_targets

# DO NOT DELETE THIS LINE -- make depend depends on it.

