LIBDEP=1

PLIB :=
ifneq (,$(USE_MONGO))
PLIB += -lmongoclient
endif
PLIB += -lboost_thread -lboost_filesystem -lboost_program_options


include ../../Make.rules
