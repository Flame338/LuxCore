#include "ByteIO.h"

#include <stdexcept>

void fail_format(const std::string& message) {
    throw std::runtime_error("LuxCore index file: " + message);
}
