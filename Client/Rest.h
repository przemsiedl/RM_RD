#ifndef RestH
#define RestH

#include <windows.h>
#include <winsock.h>
#include <vector>

class Rest {
private:
    char* host;
    int port;
    in_addr inAddr;
    bool ipResolved;

    std::vector<char> executeRequest(const char* method, const char* path, const char* body);
public:
    Rest(const char* _host, int _port = 80);
    ~Rest();

   void init();
	std::vector<char> get(const char* path);
	std::vector<char> post(const char* path, const char* body);
};

#endif