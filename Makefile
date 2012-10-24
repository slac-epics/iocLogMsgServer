#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
#     National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
#     Operator of Los Alamos National Laboratory.
# EPICS BASE Versions 3.13.7
# and higher are distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution. 
#*************************************************************************
TOP=../../..
# TOP FOR TESTING!!!
# TOP=../..
include $(TOP)/configure/CONFIG

#
# Added winmm user32 for the non-dll build
#
#PROD_HOST_DEFAULT = iocLogMsgServer msg2Oracle
PROD_HOST_DEFAULT = iocLogMsgServer msgReceiver 
PROD_HOST_WIN32   = iocLogMsgServer
PROD_SYS_LIBS_WIN32 = ws2_32 advapi32 user32
PROD_LIBS = Com ca

#USR_LDFLAGS_DEFAULT += -L/afs/slac/package/oracle/d/linux/11.1.0/lib
#USR_CFLAGS = -O0

#msg2Oracle_CFLAGS += -I/afs/slac/package/oracle/d/linux/11.1.0/precomp/public
#msg2Oracle_CFLAGS += -L/afs/slac/package/oracle/d/linux/11.1.0/precomp/lib
#msg2Oracle_CFLAGS += -L/afs/slac/package/oracle/d/linux/11.1.0/lib

USR_LDFLAGS_DEFAULT += -L$(ORACLE_HOME)/lib
USR_CFLAGS = -O0

msg2Oracle_CFLAGS += -I$(ORACLE_HOME)/precomp/public
msg2Oracle_CFLAGS += -L$(ORACLE_HOME)/precomp/lib
msg2Oracle_CFLAGS += -L$(ORACLE_HOME)/lib


iocLogMsgServer_SRCS = iocLogMsgServer.c
iocLogMsgServer_SRCS += msg2Oracle.c
iocLogMsgServer_SRCS += MessageLoggerTest.cpp

#iocLogMsgServer_SRCS += msgSender.c

msgReceiver_SRCS += msgReceiver.c

iocLogMsgServer_SYS_LIBS_solaris = socket
# if I put these in, the g++ fails
iocLogMsgServer_SYS_LIBS += clntsh
# this one works too
#iocLogMsgServer_SYS_LIBS += sqlplus

include $(TOP)/configure/RULES

#	EOF Makefile.Host for base/src/util
