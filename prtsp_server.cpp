#include "prtsp_server.h"
#include "plog.h"
#include "prtsp_conn.h"

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

PRtspServer::PRtspServer(PString& ip, uint16_t port)
	: m_ip(ip)
	, m_port(port)
	, m_sock(-1)
{

}

PRtspServer::~PRtspServer()
{
	if (m_sock > 0)
	{
		close(m_sock);
	}
}

void PRtspServer::OnRun()
{
	m_sock = socket(AF_INET, SOCK_STREAM, 0);
	
	if (m_sock < 0)
	{
		P_LOG("create socket error:%d", errno);
		return;
	}

	int val = 1;

	if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)))
	{
		P_LOG("set socketopt error:%d", errno);
		return;
	}

	struct sockaddr_in server_addr;
	
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(m_ip.c_str());
	server_addr.sin_port = htons(m_port);

	if (bind(m_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)))
	{
		P_LOG("bind socket error:%d", errno);
		return;
	}

	if (listen(m_sock, 128))
	{
		P_LOG("listen socket error:%d", errno);
		return;
	}

	sockaddr_in in_addr;
	socklen_t in_addrlen;
	int fd;

	for (;;)
	{
		in_addrlen = sizeof(sockaddr_in);

		fd = accept(m_sock, (struct sockaddr*)&in_addr, &in_addrlen);

		if (fd < 0)
		{
			P_LOG("accept socket error:%d", errno);
			return;
		}
		else
		{
			PRtspConn* conn = new PRtspConn(fd);
			conn->Start();
		}
	}
}
