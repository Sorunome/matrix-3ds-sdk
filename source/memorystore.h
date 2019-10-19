#ifndef _memorystore_h_
#define _memorystore_h_

#include "../include/matrixclient.h"
#include <string>

namespace Matrix {

class MemoryStore : public Store {
private:
	std::string syncToken = "";
	std::string filterId = "";
public:
	void setSyncToken(std::string token);
	std::string getSyncToken();
	void setFilterId(std::string fid);
	std::string getFilterId();
};

}; // namespace Matrix

#endif // _memorystore_h_
