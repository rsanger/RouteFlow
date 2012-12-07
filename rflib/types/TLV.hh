#ifndef __TLV_HH__
#define __TLV_HH__

#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>
#include <mongo/client/dbclient.h>

class TLV {
    public:
        TLV(TLV *other);
        TLV(uint8_t type, size_t len, const uint8_t *value);
        TLV(uint8_t type, size_t len, const uint32_t value);
        ~TLV();

        uint8_t getType() const;
        size_t getLength() const;
        const uint8_t *getValue() const;

        virtual std::string toString() const;
        virtual std::string type_to_string(uint8_t type) const;
        virtual mongo::BSONObj to_BSON() const;

    protected:
        uint8_t type;
        size_t length;
        uint8_t *value;

    private:
        void init(uint8_t type, size_t len, const uint8_t *value);
};

#endif /* __TLV_HH__ */
