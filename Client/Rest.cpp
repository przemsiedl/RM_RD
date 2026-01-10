#include "Rest.h"
#include <stdio.h>
#include "..\Frame.h"

Rest::Rest(const char* _host, int _port) {
    host = new char[strlen(_host) + 1];
    strcpy(host, _host);
    port = _port;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(1, 1), &wsaData);
}

Rest::~Rest() {
    delete[] host;
    WSACleanup();
}

void Rest::init()
{
	LPHOSTENT h = gethostbyname(host);
   if(!h || !h->h_addr_list || !h->h_addr_list[0])
   {
   	ipResolved = false;
      return;
   }

   memcpy(&inAddr, h->h_addr_list[0], sizeof(in_addr));
   ipResolved = true;
}

std::vector<char> Rest::executeRequest(const char* method, const char* path, const char* body)
{
	if (!ipResolved)
	{
		return NULL;
	}

   SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

   SOCKADDR_IN sockAddr;
   sockAddr.sin_family = AF_INET;
   sockAddr.sin_port = htons(port);
	sockAddr.sin_addr = inAddr;

	if (connect(s, (PSOCKADDR)&sockAddr, sizeof(sockAddr)) != 0) {
		closesocket(s);
		return NULL;
	}

	char request[2048];
	int reqLen = sprintf(	request,
									"%s %s HTTP/1.1\r\n"
        							"Host: %s\r\n"
        							"Content-Length: %d\r\n"
        							"Content-Type: application/x-www-form-urlencoded\r\n"
        							"Connection: close\r\n\r\n",
        							method,
                           path,
                           host,
                           (body ? strlen(body) : 0));

	send(	s,
   		request,
         reqLen,
         0);
	if (body)
	{
		send(	s,
      		body,
            strlen(body),
            0);
	}

	std::vector<char> responseBuffer;
   responseBuffer.reserve(5242880);
	char tempBuffer[65536];
	int nData;

	while ((nData = recv(s, tempBuffer, sizeof(tempBuffer), 0)) > 0)
   {
		responseBuffer.insert(responseBuffer.end(), tempBuffer, tempBuffer + nData);
	}
	responseBuffer.push_back('\0');

	closesocket(s);
	return responseBuffer;
}

std::vector<char> Rest::get(const char* path) {
	return executeRequest("GET", path, NULL);
}

std::vector<char> Rest::post(const char* path, const char* body) {
	return executeRequest("POST", path, body);
}