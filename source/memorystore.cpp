#include "memorystore.h"
#include <string>

namespace Matrix {

void MemoryStore::setSyncToken(std::string token) {
	syncToken = token;
}

std::string MemoryStore::getSyncToken() {
	return syncToken;
}

void MemoryStore::setFilterId(std::string fid) {
	filterId = fid;
}

std::string MemoryStore::getFilterId() {
	return filterId;
}

}; // namespace Matrix
