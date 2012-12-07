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
    uint32_t value_nbo = htonl(value);

    this->init(type, len, (uint8_t *)&value_nbo);
}

TLV::~TLV() {
    if (this->value != NULL)
        delete [] this->value;
}

uint8_t TLV::getType() const {
    return this->type;
}

const uint8_t *TLV::getValue() const {
    return this->value;
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
                          this->value);
    return builder.obj();
}

std::string TLV::toString() const {
    char buf[this->length];
    snprintf(buf, this->length, "%*s", (int)this->length, this->value);

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

/**
 * Constructs a new TLV object based on the given BSONObj.
 *
 * It is the caller's responsibility to free the returned object. If the given
 * BSONObj is not a valid TLV, this method returns NULL.
 */
TLV* TLV::from_BSON(const mongo::BSONObj bson) {
    mongo::BSONElement btype = bson["type"];
    mongo::BSONElement bvalue = bson["value"];

    if (btype.type() != mongo::NumberInt)
        return NULL;

    if (bvalue.type() != mongo::BinData)
        return NULL;

    uint8_t type = (uint8_t)btype["type"].Int();
    int len = bvalue.valuesize();
    const char *value = bvalue.binData(len);

    return new TLV(type, len, (const uint8_t *)value);
}

void TLV::init(uint8_t type, size_t len, const uint8_t *value) {
    this->type = type;
    this->value = NULL;
    this->length = 0;

    if (len == 0 || value == NULL) {
        return;
    }

    this->value = new uint8_t[len];
    memcpy(this->value, value, len);
    this->length = len;
}
