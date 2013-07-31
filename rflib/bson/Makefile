LIBDEP=1

PLIB :=

API_MONGO_DIR := ../../api-mongo
API_MONGO_SOURCES := base/error_codes.cpp base/parse_number.cpp \
	base/status.cpp bson/oid.cpp db/jsobj.cpp

SOURCE_FILES := $(patsubst %.cpp,%.cc, $(notdir $(API_MONGO_SOURCES)))

$(BUILD_OBJ_DIR)/$(notdir $(CURDIR))/error_codes.o: $(API_MONGO_DIR)/base/error_codes.cpp
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c  $< -o $@

$(BUILD_OBJ_DIR)/$(notdir $(CURDIR))/parse_number.o: $(API_MONGO_DIR)/base/parse_number.cpp
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c  $< -o $@

$(BUILD_OBJ_DIR)/$(notdir $(CURDIR))/status.o: $(API_MONGO_DIR)/base/status.cpp
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c  $< -o $@

$(BUILD_OBJ_DIR)/$(notdir $(CURDIR))/oid.o: $(API_MONGO_DIR)/bson/oid.cpp
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c  $< -o $@

$(BUILD_OBJ_DIR)/$(notdir $(CURDIR))/jsobj.o: $(API_MONGO_DIR)/db/jsobj.cpp
	$(CPP) $(CFLAGS) $(CPPFLAGS) -c  $< -o $@


include ../../Make.rules
