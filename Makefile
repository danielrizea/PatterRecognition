 
################

# Subdirectories

################

 

DIRS := spu

 

################

# Target

################

 

PROGRAM_ppu := clasificare

 

################

# Local Defines

################

 

IMPORTS          := spu/lib_simple_spu.a -lspe2 -lpthread

# imports the embedded simple_spu library

# allows consolidation of spu program into ppe binary

 

 

################

# make.footer

################

 

# make.footer is in the top of the SDK

ifdef CELL_TOP

include $(CELL_TOP)/buildutils/make.footer

else

include ../../../../buildutils/make.footer

endif
