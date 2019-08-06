#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
#     National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
#     Operator of Los Alamos National Laboratory.
# EPICS BASE Versions 3.13.7
# and higher are distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution. 
#*************************************************************************
TOP=.
include $(TOP)/configure/CONFIG

#
# Added winmm user32 for the non-dll build
#
#PROD_HOST_DEFAULT = iocLogMsgServer msg2Oracle
PROD_HOST_DEFAULT = iocLogMsgServer msgReceiver 
PROD_HOST_WIN32   = iocLogMsgServer
PROD_SYS_LIBS_WIN32 = ws2_32 advapi32 user32
#PROD_LIBS = Com ca
PROD_LIBS   += $(EPICS_BASE_HOST_LIBS)

USR_LDFLAGS_DEFAULT += -L$(ORACLE_HOME)/lib
USR_LDFLAGS_DEFAULT += -L$(ORACLE_HOME)/precomp/lib
USR_CFLAGS = -O0
USR_CFLAGS += -I$(ORACLE_HOME)/precomp/public
USR_CFLAGS += -L$(ORACLE_HOME)/precomp/lib
USR_CFLAGS += -L$(ORACLE_HOME)/lib

iocLogMsgServer_SRCS = iocLogMsgServer.c
iocLogMsgServer_SRCS += msg2Oracle.c
iocLogMsgServer_SRCS += MessageLoggerTest.cpp

#iocLogMsgServer_SRCS += msgSender.c

msgReceiver_SRCS += msgReceiver.c

iocLogMsgServer_SYS_LIBS_solaris = socket
# if I put these in, the g++ fails
iocLogMsgServer_SYS_LIBS += clntsh
iocLogMsgServer_SYS_LIBS += nnz11
# this one works too
#iocLogMsgServer_SYS_LIBS += sqlplus

include $(TOP)/configure/RULES
include $(TOP)/configure/RULES_TOP

GCC_VERSION=`gcc -dumpversion`

msg2Oracle.c:
	cp ../msg2Oracle.pc .
	LD_LIBRARY_PATH=$(ORACLE_HOME)/precomp/lib:$(ORACLE_HOME)/lib:$(LD_LIBRARY_PATH) $(ORACLE_HOME)/bin/proc sys_include=/usr/lib/gcc/x86_64-redhat-linux/$(GCC_VERSION)/include/ common_parser=yes msg2Oracle.pc
	mv msg2Oracle.c ../.

echo:
	echo $(ORACLE_HOME)
