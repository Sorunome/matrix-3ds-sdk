#include "memorystore.h"
#include <string>

namespace Matrix {

MemoryStore::MemoryStore() {
	syncToken = "";
}

void MemoryStore::setSyncToken(std::string token) {
	syncToken = token;
}

std::string MemoryStore::getSyncToken() {
	return syncToken;
}

}; // namespace Matrix
