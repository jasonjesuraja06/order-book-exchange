// Prints the real in-memory size of the hot structs and the wire messages.
// The README quotes these numbers; this target is how they are obtained.

#include "core/Order.h"
#include "network/Protocol.h"

#include <cstdio>

int main() {
    using namespace exchange;

    std::printf("sizeof(Order)            = %zu bytes (alignof %zu)\n",
                sizeof(Order), alignof(Order));
    std::printf("sizeof(Trade)            = %zu bytes\n", sizeof(Trade));
    std::printf("Orders per 64-byte line  = %zu\n", 64 / sizeof(Order));
    std::printf("sizeof(OrderMessage)     = %zu bytes\n", sizeof(OrderMessage));
    std::printf("sizeof(CancelMessage)    = %zu bytes\n", sizeof(CancelMessage));
    std::printf("sizeof(ExecutionReport)  = %zu bytes\n", sizeof(ExecutionReport));
    return 0;
}
