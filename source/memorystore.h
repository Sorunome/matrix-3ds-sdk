#ifndef _memorystore_h_
#define _memorystore_h_

#include "../include/matrixclient.h"
#include <string>

namespace Matrix {

class MemoryStore : public Store {
private:
	std::string syncToken;
public:
	MemoryStore();
	void setSyncToken(std::string token);
	std::string getSyncToken();
};

}; // namespace Matrix

#endif // _memorystore_h_
