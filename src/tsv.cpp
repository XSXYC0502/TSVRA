#include "tsv.hpp"

namespace tsvra {

TSV::TSV()
    : x_(0), y_(0), z_(0), is_redundant_(false), is_failed_(false), available_at_(0), usage_count_(0) {
}

TSV::TSV(int32_t x, int32_t y, int32_t z, bool is_redundant)
    : x_(x), y_(y), z_(z), is_redundant_(is_redundant), is_failed_(false), available_at_(0), usage_count_(0) {
}

} // namespace tsvra

