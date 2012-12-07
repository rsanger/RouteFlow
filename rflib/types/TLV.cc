#include <arpa/inet.h>
#include "TLV.hh"

/**
 * Constructs a new TLV object based on the given TLV
 */
TLV::TLV(TLV *other) {
    this->init(other->getType(), other->getLength(), other->getValue());
}

/**
 * Constructs a new TLV object by copying the value from the given pointer.
 */
TLV::TLV(uint8_t type, size_t len, const uint8_t *value) {
    this->init(type, len, value);
}

/**
 * Constructs a new TLV object using the given value.
 *
 * Stores the uint32 in network byte order.
 */
TLV::TLV(uint8_t type, size_t len, const uint32_t value) {
    this->init(type, len, (uint8_t *)&value);
}

uint8_t TLV::getType() const {
    return this->type;
}

const uint8_t *TLV::getValue() const {
    return this->value.get();
}

size_t TLV::getLength() const {
    return this->length;
}

/**
 * Serialises the TLV object to BSON in the following format:
 * {
 *   "type": (int),
 *   "value": (binary)
 * }
 */
mongo::BSONObj TLV::to_BSON() const {
    mongo::BSONObjBuilder builder;

    builder.append("type", this->type);
    builder.appendBinData("value", this->length, mongo::BinDataGeneral,
                          this->value.get());
    return builder.obj();
}

std::string TLV::toString() const {
    char buf[this->length];
    snprintf(buf, this->length, "%*s", (int)this->length, this->value.get());

    std::stringstream ss;
    ss << "{\"type\": " << this->type_to_string(this->type) << ", \"value\":\"";
    ss.write(buf, this->length);
    ss << "\"}";

    return ss.str();
}

std::string TLV::type_to_string(uint8_t type) const {
    std::stringstream ss;
    ss << type;
    return ss.str();
}

void TLV::init(uint8_t type, size_t len, const uint8_t *value) {
    this->type = type;
    this->length = 0;

    if (len == 0 || value == NULL) {
        return;
    }

    this->value.reset(new uint8_t[len]);
    memcpy(this->value.get(), value, len);
    this->length = len;
}
