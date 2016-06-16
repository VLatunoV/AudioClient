/*************************
*  Valentin Latunov 2016 *
*      SOCKET LAYER      *
*************************/

#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

namespace socketLayer
{
	enum FAILSTATE
	{
		_NO_ERROR = 0,
		FAILED_TO_INITIALIZE,
		FAILED_TO_SET_ADDR_PARAMS,
		FAILED_TO_CREATE_SOCKET,
		FAILED_TO_CONNECT,
		FAILED_TO_CLOSE_SOCKET,
		FAILED_TO_CREATE_LISTENING_SOCKET,
		FAILED_TO_BIND_LISTENING_SOCKET,
		FAILED_TO_SET_LISTENER,
		FAILED_TO_ACCEPT_CONNECTION,
		LISTENER_NOT_BOUND,
		SOCKET_CLOSED,
		FAILED_TO_SEND,
		FAILED_TO_RECEIVE,
		ADDRESS_IN_USE
	};
	FAILSTATE last_error = _NO_ERROR;
	bool initialized = false;
	WSADATA wsaData;
	addrinfo addr_hint;
	addrinfo* LISTENER_ADDRINFO = NULL;
	SOCKET LISTENER = INVALID_SOCKET;
	bool listener_bound = false;
	class Connection
	{
		SOCKET sock;
		sockaddr_in connected_sock_info;
		addrinfo* sock_info;

		void copy_addrinfo(const addrinfo& other)
		{
			*this->sock_info = other;
			this->sock_info->ai_addr = new sockaddr;
			*this->sock_info->ai_addr = *other.ai_addr;
			this->sock_info->ai_next = NULL;
		}
	public:
		Connection(): sock(INVALID_SOCKET), sock_info(NULL){}
		Connection(const Connection& other)
		{
			*this = other;
		}
		~Connection()
		{
			closesocket(sock);
			if (sock_info != NULL)
			{
				freeaddrinfo(sock_info);
			}
		}
		Connection& operator= (const Connection& other)
		{
			if (this != &other)
			{
				this->sock = other.sock;
				this->connected_sock_info = other.connected_sock_info;
				if (this->sock_info != NULL)
				{
					freeaddrinfo(this->sock_info);
				}
				this->sock_info = new addrinfo;
				copy_addrinfo(*other.sock_info);
			}
			return *this;
		}

		bool Connect(const char* IP, const char* PORT)
		{
			if (sock_info)
			{
				freeaddrinfo(sock_info);
				sock_info = NULL;
			}
			addr_hint.ai_flags = AI_CANONNAME;
			if (getaddrinfo(IP, PORT, &addr_hint, &sock_info))
			{
				last_error = FAILED_TO_SET_ADDR_PARAMS;
				return false;
			}
			sock = socket(sock_info->ai_family, sock_info->ai_socktype, sock_info->ai_protocol);
			if (sock == INVALID_SOCKET)
			{
				last_error = FAILED_TO_CREATE_SOCKET;
				return false;
			}
			if (connect(sock, sock_info->ai_addr, (int)sock_info->ai_addrlen))
			{
				last_error = FAILED_TO_CONNECT;
				return false;
			}
			return true;
		}
		bool Accept()
		{
			if (!listener_bound)
			{
				last_error = LISTENER_NOT_BOUND;
				return false;
			}
			if (listen(LISTENER, SOMAXCONN))
			{
				last_error = FAILED_TO_SET_LISTENER;
				return false;
			}
			int size = sizeof(connected_sock_info);
			sock = accept(LISTENER, (sockaddr*)&connected_sock_info, &size);
			if (sock == INVALID_SOCKET)
			{
				last_error = FAILED_TO_ACCEPT_CONNECTION;
				return false;
			}
			return true;
		}
		bool Disconnect()
		{
			if (closesocket(sock))
			{
				last_error = FAILED_TO_CLOSE_SOCKET;
				return false;
			}
			return true;
		}
		bool Send(const char* buf, unsigned int size)
		{
			if (sock == INVALID_SOCKET)
			{
				last_error = FAILED_TO_SEND;
				return false;
			}
			int status = send(sock, buf, size, 0);
			if (status == 0)
			{
				last_error = SOCKET_CLOSED;
				return false;
			}
			else if (status == SOCKET_ERROR)
			{
				last_error = FAILED_TO_SEND;
				return false;
			}
			return true;
		}
		bool Recv(char* buf, unsigned int size, int& recv_size)
		{
			if (sock == INVALID_SOCKET)
			{
				last_error = FAILED_TO_RECEIVE;
				return false;
			}
			int status = recv(sock, buf, size, 0);
			if (status == 0)
			{
				last_error = SOCKET_CLOSED;
				return false;
			}
			else if (status == SOCKET_ERROR)
			{
				last_error = FAILED_TO_RECEIVE;
				return false;
			}
			recv_size = status;
			return true;
		}
		char* GetConnectionIP() const
		{
			if (sock_info)
			{
				return inet_ntoa(((sockaddr_in*)sock_info->ai_addr)->sin_addr);
			}
			else
			{
				return inet_ntoa(connected_sock_info.sin_addr);
			}
		}
		unsigned int GetConnectionPort() const
		{
			if (sock_info)
			{
				return ((sockaddr_in*)sock_info->ai_addr)->sin_port;
			}
			else
			{
				return connected_sock_info.sin_port;
			}
		}
	};
	bool Initialize()
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData))
		{
			last_error = FAILED_TO_INITIALIZE;
			return false;
		}
		ZeroMemory(&addr_hint, sizeof(addr_hint));
		addr_hint.ai_family = AF_INET;
		addr_hint.ai_socktype = SOCK_STREAM;
		addr_hint.ai_protocol = IPPROTO_TCP;
		LISTENER = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (LISTENER == INVALID_SOCKET)
		{
			last_error = FAILED_TO_CREATE_LISTENING_SOCKET;
			return false;
		}
		initialized = true;
		return true;
	}
	void Cleanup()
	{
		closesocket(LISTENER);
		WSACleanup();
		initialized = false;
	}
	bool Bind(const char* PORT)
	{
		addr_hint.ai_flags = AI_PASSIVE;
		if (getaddrinfo(NULL, PORT, &addr_hint, &LISTENER_ADDRINFO))
		{
			last_error = FAILED_TO_SET_ADDR_PARAMS;
			return false;
		}
		if (bind(LISTENER, LISTENER_ADDRINFO->ai_addr, (int)LISTENER_ADDRINFO->ai_addrlen))
		{
			if (WSAGetLastError() == WSAEADDRINUSE)
			{
				last_error = ADDRESS_IN_USE;
			}
			else
			{
				last_error = FAILED_TO_BIND_LISTENING_SOCKET;
			}
			return false;
		}
		listener_bound = true;
		return true;
	}
	const char* StringError(FAILSTATE err = last_error)
	{
		if (!initialized)
		{
			return "Socket layer is not initialized.\n";
		}
		switch (err)
		{
		case _NO_ERROR: return "No error occured.\n";
		case FAILED_TO_INITIALIZE: return "Failed to initialize socket layer.\n";
		case FAILED_TO_SET_ADDR_PARAMS: return "Failed to set connection address parameters.\n";
		case FAILED_TO_CREATE_SOCKET: return "Failed to create socket.\n";
		case FAILED_TO_CONNECT: return "Failed to connect.\n";
		case FAILED_TO_CLOSE_SOCKET: return "Failed to close socket.\n";
		case FAILED_TO_CREATE_LISTENING_SOCKET: return "Failed to create listening socket.\n";
		case FAILED_TO_BIND_LISTENING_SOCKET: return "Failed to bind listening socket.\n";
		case FAILED_TO_SET_LISTENER: return "Listening socket is unable to receive connection requests.\n";
		case FAILED_TO_ACCEPT_CONNECTION: return "Listening socket failed to accept connection.\n";
		case LISTENER_NOT_BOUND: return "Listening socket is not bound to any port.\n";
		case SOCKET_CLOSED: return "Socket is already closed.\n";
		case FAILED_TO_SEND: return "Failed to send.\n";
		case FAILED_TO_RECEIVE: return "Failed to receive.\n";
		case ADDRESS_IN_USE: return "Address in use.\n";
		default: return "Unknown error occured.\n";
		}
	}
	void Clear_Errors()
	{
		last_error = _NO_ERROR;
	}
}
