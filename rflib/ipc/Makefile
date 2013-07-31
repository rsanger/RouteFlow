LIBDEP=1

PLIB :=
ifneq (,$(USE_MONGO))
PLIB += -lmongoclient
endif
PLIB += -lboost_thread -lboost_filesystem -lboost_program_options

SOURCE_FILES := IPC.cc RFProtocol.cc RFProtocolFactory.cc
ifeq ($(IPC),zeromq)
SOURCE_FILES += ZeroMQIPC.cc
else ifeq ($(IPC),mongo)
SOURCE_FILES += MongoIPC.cc
endif

include ../../Make.rules
