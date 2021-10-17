#ifndef VIRTUAL_TCP_H__
#define VIRTUAL_TCP_H__

#include <string>
#include <vector>
#include <tuple>
#include <thread>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#ifdef __unix__
#	include <sys/types.h>
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <unistd.h>
#elif _WINDOWS
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	error "unknonw operation system"
#endif

enum VirtualSocketStatus
{
	VIRTUAL_SOCKET_VOID
	, VIRTUAL_SOCKET_INITIAL
	, VIRTUAL_SOCKET_CONNECT
};

enum VirtualTcpCommand: char
{
	COM_SOCKET
	, COM_CONNECT
	, COM_BIND
	, COM_LISTEN
	, COM_ACCEPT
	, COM_SEND
	, COM_RECV
	, COM_CLOSE
};

#ifdef __unix__
	using SOCKET = int;
#endif

using VIRTUAL_SOCKET = long;
const long INVALID_SOCKET = -1;

class VirtualSocketImpl
{
	public:
		static const size_t BUF_SIZE = 65536;

		std::shared_ptr<std::mutex> mtx;

		unsigned long ip;
		unsigned short port;

		std::shared_ptr<VirtualSocketImpl> partner;

		VirtualSocketStatus status;

		char buffer[BUF_SIZE];
		size_t bufend;

		VirtualSocketImpl ();
		VirtualSocketImpl (unsigned long ip_, unsigned short port_);
		VirtualSocketImpl (const VirtualSocketImpl &obj);
		~VirtualSocketImpl ();

		void connect (std::shared_ptr<VirtualSocketImpl> partner_);
		void write (const char *msg, int len);
		bool read (char *msg, int len);
		void close ();
};

class VirtualTcp
{
	private:
		static constexpr unsigned int PollingMs = 100;
		static constexpr const char *ALTERNATIVE_IP = "127.0.0.1";
		static constexpr int ALTERNATIVE_PORT = 12345;
		static bool running;
		static std::thread alternative_tcp_server_th;
		static std::vector<VirtualSocketImpl> sockets;
		static std::unordered_map<VirtualTcpCommand
			, std::function<void(SOCKET, const char *)>> services;

		SOCKET alternative_server;
		std::string virtual_addr;
		int virtual_port;

		static void alternative_tcp_server_fn ();
		static void alternative_tcp_server_service_fn (SOCKET sock);
		static void serve_socket (SOCKET sock, const char*com);
		static void serve_connect (SOCKET sock, const char*com);
		static void serve_bind (SOCKET sock, const char*com);
		static void serve_listen (SOCKET sock, const char*com);
		static void serve_accept (SOCKET sock, const char*com);
		static void serve_recv (SOCKET sock, const char*com);
		static void serve_send (SOCKET sock, const char*com);
		static void serve_close (SOCKET sock, const char*com);

	public:
		static int startup ();
		static int cleanup ();

		VirtualTcp (const std::string virtual_addr_, int virtual_port_);
		~VirtualTcp ();

		VIRTUAL_SOCKET vsocket (int af, int type, int protocol);
		int vconnect (VIRTUAL_SOCKET s, const sockaddr *name, int namelen);
		int vbind (VIRTUAL_SOCKET s, const sockaddr *name, int namelen);
		int vlisten (VIRTUAL_SOCKET s, int backlog);
		VIRTUAL_SOCKET vaccept (VIRTUAL_SOCKET s, sockaddr *addr, unsigned int *addrlen);
		int vsend (VIRTUAL_SOCKET s, const char *buf, int len, int flags);
		int vrecv (VIRTUAL_SOCKET s, char *buf, int len, int flags);
		int vclosesocket (VIRTUAL_SOCKET s);
};

#endif // VIRTUAL_TCP_H__
