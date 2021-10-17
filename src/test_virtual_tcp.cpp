#include <iostream>
#include <thread>
#include <string.h>
#include "virtual_tcp.h"


void server_fn ()
{
	VirtualTcp vtcp("192.168.3.51", 501);

	VIRTUAL_SOCKET vsock0 = vtcp.vsocket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(501);
#ifdef __unix__
	addr.sin_addr.s_addr = INADDR_ANY;
#elif _WINDOWS
	addr.sin_addr.S_un.S_addr = INADDR_ANY;
#endif
	vtcp.vbind(vsock0, (struct sockaddr *)&addr, sizeof(addr));

	vtcp.vlisten(vsock0, 5);

	struct sockaddr_in client;
	unsigned int len = sizeof(client);
	VIRTUAL_SOCKET vsock = vtcp.vaccept(vsock0, (struct sockaddr *)&client, &len);

	char msg[64];
	memset(msg, '\0', sizeof(msg));
	vtcp.vrecv(vsock, msg, sizeof(msg), 0);

	vtcp.vsend(vsock, "bye.", 4, 0);

	vtcp.vclosesocket(vsock);
}

void client_fn ()
{
	VirtualTcp vtcp("192.168.3.56", 501);

	VIRTUAL_SOCKET vsock = vtcp.vsocket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_port = htons(501);
#ifdef __unix__
	server.sin_addr.s_addr = inet_addr("192.168.3.51");
#elif _WINDOWS
	server.sin_addr.S_un.S_addr = inet_addr("192.168.3.51");
#endif
	vtcp.vconnect(vsock, (struct sockaddr *)&server, sizeof(server));

	char msg[64];
	memset(msg, '\0', sizeof(msg));
	sprintf(msg, "hello, world!");
	vtcp.vsend(vsock, msg, sizeof(msg), 0);
	std::cout << "SEND: " << msg << std::endl;

	memset(msg, '\0', sizeof(msg));
	vtcp.vrecv(vsock, msg, sizeof(msg), 0);
	std::cout << "RECV: " << msg << std::endl;
}

int main (int argc, char **argv)
{
	VirtualTcp::startup();

	std::thread server_th(server_fn);
	std::thread client_th(client_fn);

	server_th.join();
	client_th.join();

	VirtualTcp::cleanup();

	return 0;
}
