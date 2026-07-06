#include "smcluster.hpp"

shared_variable::shared_variable(void *address, size_t size) : address((unsigned char *)address), size(size) {}

shared_variable::~shared_variable() {}
