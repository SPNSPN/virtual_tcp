#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <iostream>
#include <algorithm>
#include <chrono>
#include "virtual_tcp.h"

VirtualSocketImpl::VirtualSocketImpl ()
	: mtx(std::make_shared<std::mutex>())
	  , ip(0)
	  , port(0)
	  , partner()
	  , status(VIRTUAL_SOCKET_VOID)
	  , buffer()
	  , bufend(0)
{
	memset(buffer, '\0', BUF_SIZE);
}

VirtualSocketImpl::VirtualSocketImpl (unsigned long ip_, unsigned short port_)
	: mtx(std::make_shared<std::mutex>())
	  , ip(ip_)
	  , port(port_)
	  , partner()
	  , status(VIRTUAL_SOCKET_VOID)
	  , buffer()
	  , bufend(0)
{
	memset(buffer, '\0', BUF_SIZE);
}

VirtualSocketImpl::VirtualSocketImpl (const VirtualSocketImpl &obj)
	: mtx(obj.mtx)
	  , ip(obj.ip)
	  , port(obj.port)
	  , partner(obj.partner)
	  , status(obj.status)
	  , buffer()
	  , bufend(obj.bufend)
{
	memcpy(buffer, obj.buffer, BUF_SIZE);
}

VirtualSocketImpl::~VirtualSocketImpl ()
{
	partner.reset();
}

void VirtualSocketImpl::connect (std::shared_ptr<VirtualSocketImpl> partner_)
{
	std::lock_guard<std::mutex> lock(*mtx);

	partner = partner_;
	status = VIRTUAL_SOCKET_CONNECT;
}

void VirtualSocketImpl::write (const char *msg, int len)
{
	std::lock_guard<std::mutex> lock(*mtx);

	size_t nbufend = bufend + len;
	if (nbufend > BUF_SIZE) { nbufend = BUF_SIZE; }

	memcpy(&(buffer[bufend]), msg, nbufend - bufend);

	bufend = nbufend;
}

bool VirtualSocketImpl::read (char *msg, int len)
{
	if (len > (int)BUF_SIZE) { len = (int)BUF_SIZE; }
	if (bufend < len) { return false; }

	std::lock_guard<std::mutex> lock(*mtx);

	memcpy(msg, buffer, len);

	char temp[BUF_SIZE];
	memcpy(temp, &(buffer[len]), BUF_SIZE - len);
	memset(buffer, '\0', BUF_SIZE);
	memcpy(buffer, temp, BUF_SIZE - len);

	bufend -= len;
	return true;
}

void VirtualSocketImpl::close ()
{
	std::lock_guard<std::mutex> lock(*mtx);

	status = VIRTUAL_SOCKET_INITIAL;
	partner.reset();
	memset(buffer, '\0', BUF_SIZE);
	bufend = 0;
}

bool VirtualTcp::running;
std::thread VirtualTcp::alternative_tcp_server_th;
std::vector<VirtualSocketImpl> VirtualTcp::sockets;
std::unordered_map<VirtualTcpCommand
	, std::function<void(SOCKET, const char *)>> VirtualTcp::services
	= {{COM_SOCKET, VirtualTcp::serve_socket}
		, {COM_CONNECT, VirtualTcp::serve_connect}
		, {COM_BIND, VirtualTcp::serve_bind}
		, {COM_LISTEN, VirtualTcp::serve_listen}
		, {COM_ACCEPT, VirtualTcp::serve_accept}
		, {COM_SEND, VirtualTcp::serve_send}
		, {COM_RECV, VirtualTcp::serve_recv}
		, {COM_CLOSE, VirtualTcp::serve_close}};

void VirtualTcp::alternative_tcp_server_fn ()
{
	SOCKET sock0;
	sock0 = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ALTERNATIVE_PORT);
#ifdef __unix__
	addr.sin_addr.s_addr = INADDR_ANY;
#elif _WINDOWS
	addr.sin_addr.S_un.S_addr = INADDR_ANY;
#endif
	bind(sock0, (struct sockaddr *)&addr, sizeof(addr));

	listen(sock0, 5);

	while (VirtualTcp::running)
	{
		struct sockaddr_in client;
		unsigned int len = sizeof(client);
		SOCKET sock = accept(sock0, (struct sockaddr *)&client, &len);

		std::thread th = std::thread(VirtualTcp::alternative_tcp_server_service_fn, sock);
		th.detach();
	}

#ifdef __unix__
	close(sock0);
#elif _WINDOWS
	closesocket(sock0);
#endif
}


void VirtualTcp::alternative_tcp_server_service_fn (SOCKET sock)
{
	while (VirtualTcp::running)
	{
		char com[1];
		memset(com, '\0', 1);
		int n = recv(sock, com, 1, 0);
		if (n <= 0) { continue; }

		VirtualTcp::services.at((VirtualTcpCommand)com[0])(sock, com);
	}

#ifdef __unix__
	close(sock);
#elif _WINDOWS
	closesocket(sock);
#endif
}

void VirtualTcp::serve_socket (SOCKET sock, const char*com)
{
	char aft[4 + 2];
	memset(aft, '\0', 6);
	int n = recv(sock, aft, 6, 0);
	if (6 != n) { ; }

	unsigned long ip = 0;
	ip |= aft[0] << 24;
	ip |= aft[1] << 16;
	ip |= aft[2] << 8;
	ip |= aft[3];

	unsigned short port = 0;
	port |= aft[4] << 8;
	port |= aft[5];

	VirtualTcp::sockets.push_back(VirtualSocketImpl(ip, port));
	VIRTUAL_SOCKET ns = VirtualTcp::sockets.size() - 1;

	char ans[4];
	memset(ans, '\0', 4);
	ans[0] = (ns & 0xff000000) >> 24;
	ans[1] = (ns & 0x00ff0000) >> 16;
	ans[2] = (ns & 0x0000ff00) >> 8;
	ans[3] = (ns & 0x000000ff);
	send(sock, ans, 4, 0);
}

void VirtualTcp::serve_connect (SOCKET sock, const char*com)
{
	char aft[4 + 4 + 2];
	memset(aft, '\0', 10);
	recv(sock, aft, 10, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	unsigned long ip = 0;
	ip |= aft[4] << 24;
	ip |= aft[5] << 16;
	ip |= aft[6] << 8;
	ip |= aft[7];

	unsigned short port = 0;
	port |= aft[8] << 8;
	port |= aft[9];

	std::vector<VirtualSocketImpl>::iterator partner;

	// 接続先がINITIALになるまで(listenになるまで)待つ
	while (VirtualTcp::running)
	{
		partner = std::find_if(VirtualTcp::sockets.begin()
				, VirtualTcp::sockets.end()
				, [=](const VirtualSocketImpl &s)
				{
					return (ip == s.ip)
						&& (port == s.port)
						&& (VIRTUAL_SOCKET_INITIAL == s.status);
				});
		if (partner != VirtualTcp::sockets.end()) { break; }
		std::this_thread::sleep_for(
				std::chrono::milliseconds(VirtualTcp::PollingMs));
	}

	char ans[2];
	memset(ans, '\0', 2);
	// TODO: vsock.statusがCONNECTのときの動作
	partner->connect(std::shared_ptr<VirtualSocketImpl>(&*vsock));
	vsock->connect(std::shared_ptr<VirtualSocketImpl>(&*partner));

	send(sock, ans, 2, 0);
}

void VirtualTcp::serve_bind (SOCKET sock, const char*com)
{
	char aft[4 + 4 + 2];
	memset(aft, '\0', 10);
	recv(sock, aft, 10, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	unsigned long ip = 0;
	ip |= aft[4] << 24;
	ip |= aft[5] << 16;
	ip |= aft[6] << 8;
	ip |= aft[7];

	unsigned short port = 0;
	port |= aft[8] << 8;
	port |= aft[9];

	// TODO: client 接続許可範囲の設定
	// TODO: statusがCONNECTのときの動作

	char ans[2];
	memset(ans, '\0', 2);
	send(sock, ans, 2, 0);
}

void VirtualTcp::serve_listen (SOCKET sock, const char*com)
{
	char aft[4];
	memset(aft, '\0', 4);
	recv(sock, aft, 4, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	// TODO: backlogの設定
	// TODO: statusがCONNECTのときの動作

	char ans[2];
	memset(ans, '\0', 2);
	send(sock, ans, 2, 0);
}

void VirtualTcp::serve_accept (SOCKET sock, const char*com)
{
	char aft[4];
	memset(aft, '\0', 4);
	recv(sock, aft, 4, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	vsock->status = VIRTUAL_SOCKET_INITIAL;

	// connect要求を待つ
	while (VirtualTcp::running)
	{
		if (VIRTUAL_SOCKET_CONNECT == vsock->status) { break; }
		std::this_thread::sleep_for(
				std::chrono::milliseconds(VirtualTcp::PollingMs));
	}

	VIRTUAL_SOCKET client;
	for (client = 0
			; client < (long)VirtualTcp::sockets.size()
			; ++client)
	{
		if (&(VirtualTcp::sockets.at(client)) == &*(vsock->partner)) { break; }
	}

	char ans[4 + 4 + 2];
	memset(ans, '\0', 10);
	// client socket, ip, portをセット
	ans[0] = (client & 0xff000000) >> 24;
	ans[1] = (client & 0x00ff0000) >> 16;
	ans[2] = (client & 0x0000ff00) >> 8;
	ans[3] = (client & 0x000000ff);
	ans[4] = (vsock->partner->ip & 0xff000000) >> 24;
	ans[5] = (vsock->partner->ip & 0x00ff0000) >> 16;
	ans[6] = (vsock->partner->ip & 0x0000ff00) >> 8;
	ans[7] = (vsock->partner->ip & 0x000000ff);
	ans[8] = (vsock->partner->port & 0xff00) >> 8;
	ans[9] = (vsock->partner->port & 0x00ff);

	send(sock, ans, 10, 0);
}

void VirtualTcp::serve_send (SOCKET sock, const char*com)
{
	char aft[4 + 2];
	memset(aft, '\0', 6);
	recv(sock, aft, 6, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	int len = 0;
	len |= aft[4] << 8;
	len |= aft[5];

	char msg[len];
	memset(msg, '\0', len);
	recv(sock, msg, len, 0);

	vsock->write(msg, len);
}

void VirtualTcp::serve_recv (SOCKET sock, const char*com)
{
	char aft[4 + 2];
	memset(aft, '\0', 6);
	recv(sock, aft, 6, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	int len = 0;
	len |= aft[4] << 8;
	len |= aft[5];

	char msg[len];
	memset(msg, '\0', len);
	while (VirtualTcp::running)
	{
		if (vsock->read(msg, len)) { break; }
		std::this_thread::sleep_for(
				std::chrono::milliseconds(VirtualTcp::PollingMs));
	}

	char ans[2 + len];
	memset(ans, '\0', 2 + len);
	ans[0] = 0x00;
	ans[1] = 0x00;
	memcpy(&(ans[2]), msg, len);
	send(sock, ans, 2 + len, 0);
}

void VirtualTcp::serve_close (SOCKET sock, const char*com)
{
	char aft[4];
	memset(aft, '\0', 4);
	recv(sock, aft, 4, 0);

	VIRTUAL_SOCKET s = 0;
	s |= aft[0] << 24;
	s |= aft[1] << 16;
	s |= aft[2] << 8;
	s |= aft[3];
	auto vsock = VirtualTcp::sockets.begin() + s;

	vsock->partner->close();
	vsock->close();
}

int VirtualTcp::startup ()
{
#ifdef __unix__
	signal(SIGPIPE, SIG_IGN);
#endif

#ifdef _WINDOWS
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 0), &wsaData);
#endif

	VirtualTcp::running = true;
	VirtualTcp::alternative_tcp_server_th
		= std::thread(VirtualTcp::alternative_tcp_server_fn);

	return 0;
}

int VirtualTcp::cleanup ()
{
	VirtualTcp::sockets.clear();
	VirtualTcp::running = false;
	VirtualTcp::alternative_tcp_server_th.join();

#ifdef _WINDOWS
	WSACleanup();
#endif

	return 0;
}

VirtualTcp::VirtualTcp (const std::string virtual_addr_, const int virtual_port_)
	  : alternative_server()
	  , virtual_addr(virtual_addr_)
	  , virtual_port(virtual_port_)
{
#ifdef _WINDOWS
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 0), &wsaData);
#endif

	alternative_server = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(ALTERNATIVE_PORT);
#ifdef __unix__
	addr.sin_addr.s_addr = inet_addr(ALTERNATIVE_IP);
#elif _WINDOWS
	addr.sin_addr.S_un.S_addr = inet_addr(ALTERNATIVE_IP);
#endif

	connect(alternative_server, (struct sockaddr *)&addr, sizeof(addr));
}

VirtualTcp::~VirtualTcp ()
{
#ifdef __unix__
	close(alternative_server);
#elif _WINDOWS
	closesocket(alternative_server);
	WSACleanup();
#endif
}


VIRTUAL_SOCKET VirtualTcp::vsocket (int af, int type, int protocol)
{
	assert(AF_INET == af);
	assert(SOCK_STREAM == type);
	assert(0 == protocol);

	unsigned long ip = inet_addr(virtual_addr.c_str());
	unsigned short port = htons(virtual_port);

	char req[1 + 4 + 2];
	memset(req, '\0', 7);
	req[0] = COM_SOCKET;
	req[1] = (char)((ip & 0xff000000) >> 24);
	req[2] = (char)((ip & 0x00ff0000) >> 16);
	req[3] = (char)((ip & 0x0000ff00) >> 8);
	req[4] = (char)( ip & 0x000000ff);
	req[5] = (char)((port & 0xff00) >> 8);
	req[6] = (char)( port & 0x00ff);
	send(alternative_server, req, 7, 0);

	char ans[4];
	memset(ans, '\0', 4);
	recv(alternative_server, ans, 4, 0);
	VIRTUAL_SOCKET s = 0;
	s |= ans[0] << 24;
	s |= ans[1] << 16;
	s |= ans[2] << 8;
	s |= ans[3];

	return s;
}

int VirtualTcp::vconnect (VIRTUAL_SOCKET s, const sockaddr *name, int namelen)
{
	if (! VirtualTcp::running)
	{
#ifdef _WINDOWS
		return WSANOTINITIALISED;
#else
		return -1;
#endif
	}
	struct sockaddr_in *sname = (struct sockaddr_in *)name;
	assert(AF_INET == sname->sin_family);

#ifdef __unix__
	unsigned long ip = sname->sin_addr.s_addr;
#elif _WINDOWS
	unsigned long ip = sname->sin_addr.S_un.S_addr;
#endif
	unsigned short port = sname->sin_port;

	char req[1 + 4 + 4 + 2];
	memset(req, '\0', 11);
	req[0] = COM_CONNECT;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	req[5] = (char)((ip & 0xff000000) >> 24);
	req[6] = (char)((ip & 0x00ff0000) >> 16);
	req[7] = (char)((ip & 0x0000ff00) >> 8);
	req[8] = (char)( ip & 0x000000ff);
	req[9] = (char)((port & 0xff00) >> 8);
	req[10] = (char)(port & 0x00ff);
	send(alternative_server, req, 11, 0);

	char ans[2];
	memset(ans, '\0', 2);
	recv(alternative_server, ans, 2, 0);
	int anscode = 0;
	anscode |= ans[0] << 8;
	anscode |= ans[1];

	return anscode;
}

int VirtualTcp::vbind (VIRTUAL_SOCKET s, const sockaddr *name, int namelen)
{
	if (! VirtualTcp::running)
	{
#ifdef _WINDOWS
		return WSANOTINITIALISED;
#else
		return -1;
#endif
	}
	struct sockaddr_in *sname = (struct sockaddr_in *)name;
	assert(AF_INET == sname->sin_family);

#ifdef __unix__
	unsigned long ip = sname->sin_addr.s_addr;
#elif _WINDOWS
	unsigned long ip = sname->sin_addr.S_un.S_addr;
#endif
	unsigned short port = sname->sin_port;

	char req[1 + 4 + 4 + 2];
	memset(req, '\0', 11);
	req[0] = COM_BIND;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	req[5] = (char)((ip & 0xff000000) >> 24);
	req[6] = (char)((ip & 0x00ff0000) >> 16);
	req[7] = (char)((ip & 0x0000ff00) >> 8);
	req[8] = (char)( ip & 0x000000ff);
	req[9] = (char)((port & 0xff00) >> 8);
	req[10] = (char)(port & 0x00ff);
	send(alternative_server, req, 11, 0);

	char ans[2];
	memset(ans, '\0', 2);
	recv(alternative_server, ans, 2, 0);

	int anscode = 0;
	anscode |= ans[0] << 8;
	anscode |= ans[1];

	return anscode;
}

int VirtualTcp::vlisten (VIRTUAL_SOCKET s, int backlog)
{
	if (! VirtualTcp::running)
	{
#ifdef _WINDOWS
		return WSANOTINITIALISED;
#else
		return -1;
#endif
	}

	// TODO: backlog

	char req[1 + 4];
	memset(req, '\0', 5);
	req[0] = COM_LISTEN;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	send(alternative_server, req, 5, 0);

	char ans[2];
	recv(alternative_server, ans, 2, 0);
	int anscode = 0;
	anscode |= ans[0] << 8;
	anscode |= ans[1];

	return anscode;
}

VIRTUAL_SOCKET VirtualTcp::vaccept (VIRTUAL_SOCKET s, sockaddr *addr, unsigned int *addrlen)
{
	if (! VirtualTcp::running)
	{
#ifdef _WINDOWS
		return WSANOTINITIALISED;
#else
		return -1;
#endif
	}

	char req[1 + 4];
	memset(req, '\0', 5);
	req[0] = COM_ACCEPT;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	send(alternative_server, req, 5, 0);

	char ans[4 + 4 + 2];
	memset(ans, '\0', 10);
	recv(alternative_server, ans, 10, 0);
	VIRTUAL_SOCKET client = 0;
	client |= ans[0] << 24;
	client |= ans[1] << 16;
	client |= ans[2] << 8;
	client |= ans[3];

	unsigned long ip = 0;
	ip |= ans[4] << 24;
	ip |= ans[5] << 16;
	ip |= ans[6] << 8;
	ip |= ans[7];

	unsigned short port = 0;
	port |= ans[8] << 8;
	port |= ans[9];

	struct sockaddr_in *saddr = (struct sockaddr_in *)addr;

#ifdef __unix__
	saddr->sin_addr.s_addr = ip;
#elif _WINDOWS
	saddr->sin_addr.S_un.S_addr = ip;
#endif
	saddr->sin_port = port;
	saddr->sin_family = AF_INET;

	return client;
}

int VirtualTcp::vsend (VIRTUAL_SOCKET s, const char *buf, int len, int flags)
{
	if (! VirtualTcp::running)
	{
#ifdef _WINDOWS
		return WSANOTINITIALISED;
#else
		return -1;
#endif
	}

	char req[1 + 4 + 2 + len];
	memset(req, '\0', 7 + len);
	req[0] = COM_SEND;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	req[5] = (char)((len & 0xff00) >> 8);
	req[6] = (char)( len & 0x00ff);
	memcpy(&(req[7]), buf, len);
	int res = send(alternative_server, req, 7 + len, 0);
	return res - 7;
}

int VirtualTcp::vrecv (VIRTUAL_SOCKET s, char *buf, int len, int flags)
{
	if (! VirtualTcp::running) { return -1; }

	char req[1 + 4 + 2];
	memset(req, '\0', 7);
	req[0] = COM_RECV;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	req[5] = (char)((len & 0xff00) >> 8);
	req[6] = (char)( len & 0x00ff);
	send(alternative_server, req, 7, 0);

	char ans[2 + len];
	memset(ans, '\0', 2 + len);
	recv(alternative_server, ans, 2 + len, 0);

	int recvlen = 0;
	recvlen |= (ans[0] << 8);
	recvlen |= ans[1];
	memcpy(buf, &(ans[2]), len);

	return recvlen;
}

int VirtualTcp::vclosesocket (VIRTUAL_SOCKET s)
{
	if (! VirtualTcp::running) { return -1; }

	char req[5];
	memset(req, '\0', 5);
	req[0] = COM_CLOSE;
	req[1] = (char)((s & 0xff000000) >> 24);
	req[2] = (char)((s & 0x00ff0000) >> 16);
	req[3] = (char)((s & 0x0000ff00) >> 8);
	req[4] = (char)( s & 0x000000ff);
	send(alternative_server, req, 5, 0);

	return 0;
}
