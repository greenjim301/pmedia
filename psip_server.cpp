#include "psip_server.h"
#include "psip_conn.h"
#include "plog.h"

PSipServer::PSipServer(PString& domain, PString& pwd, PString& ip, uint16_t port)
	: m_doman(domain)
	, m_passwd(pwd)
	, m_ip(ip)
	, m_port(port)
{

}

void PSipServer::OnRun()
{
	m_sock = socket(AF_INET, SOCK_DGRAM, 0);

	if (m_sock < 0)
	{
		P_LOG("create socket error:%d", errno);
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

	this->AddInEvent(m_sock);

	parser_init();

	struct epoll_event events[DEF_MAX_EVENTS];
	std::vector<PTaskMsg> msgs;
	int ret;

	for (;;)
	{
		ret = this->WaitEvent(events, msgs, -1);

		if (ret < 0)
		{
			P_LOG("epoll wait err:%d", errno);
			return;
		}
		else
		{
			for (int i = 0; i < ret; ++i)
			{
				struct epoll_event& ev = events[i];

				if (ev.data.fd == m_sock)
				{
					if (this->udp_recv())
					{
						P_LOG("sip recv err");
						return;
					}
				}
				else
				{
					P_LOG("epoll no match fd");
					return;
				}
			}

			for (auto it = msgs.begin(); it != msgs.end(); ++it)
			{
				if (this->process_msg(*it))
				{
					return;
				}
			}

		}			
	}
}

int PSipServer::udp_recv()
{
	sockaddr_in cAddr;
	socklen_t cAddrLen = sizeof(sockaddr_in);
	char buf[2048];
	
	int ret = recvfrom(m_sock, buf, 2048, 0, (struct sockaddr *)&cAddr, &cAddrLen);
	
	if (ret <=0 )
	{
		return ret;
	}

	osip_message_t* sip;
	osip_message_init(&sip);

	ret = osip_message_parse(sip, buf, ret);

	if (ret)
	{
		P_LOG("illegal msg");
		osip_message_free(sip);
		return 0;
	}

	char* dest;
	size_t length;
	
	osip_message_to_str(sip, &dest, &length);
	
	P_LOG("message:\n%s\n", dest);	
	osip_free(dest);

	PSipConn* conn;
	PString username = sip->from->url->username;
	auto it = m_sipConn.find(username);

	if (it == m_sipConn.end())
	{
		conn = new PSipConn(this);
		m_sipConn[username] = conn;
	} 
	else
	{
		conn = it->second;		
	}

	if (conn->process_req(sip, cAddr, cAddrLen))
	{
		m_sipConn.erase(username);
		delete conn;
	}

	osip_message_free(sip);

	return 0;
}

int PSipServer::process_msg(PTaskMsg& msg)
{
	return 0;
}

const PString& PSipServer::get_domain()
{
	return m_doman;
}

const PString& PSipServer::get_passwd()
{
	return m_passwd;
}

void PSipServer::send_sip_rsp(osip_message_t* rsp, sockaddr_in& in_addr, socklen_t in_addrlen)
{
	char *dest = NULL;
	size_t length = 0;

	osip_message_to_str(rsp, &dest, &length);
	P_LOG("message:\n%s\n", dest);

	int ret = sendto(m_sock, dest, length, 0, (struct sockaddr*)&in_addr, in_addrlen);
	if (ret != length)
	{
		P_LOG("send failed");
	}

	osip_free(dest);
	osip_message_free(rsp);
}
