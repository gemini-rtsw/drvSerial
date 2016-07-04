TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

# release header files
INC += drvSerial.h

#=============================

# xxxRecord.h will be created from xxxRecord.dbd
#DBDINC += xxxRecord

# <name>.dbd will be created from <name>Include.dbd
DBD += drvSerial.dbd

#=============================

LIBRARY_IOC = drvSerial

drvSerial_SRCS += drvSerial.c
drvSerial_SRCS += drvSerialRegister.cpp

# A test facility to optionally add to an IOC
#DBD += sioTest.dbd
DBD += drvSerialTest.dbd
#LIBRARY_IOC_RTEMS += sioTest
#sioTest_SRCS += sioTest.c
#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
