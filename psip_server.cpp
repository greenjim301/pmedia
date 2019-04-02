#include "psip_server.h"
#include "psip_conn.h"
#include "plog.h"
#include <ctime>
#include "psip_client.h"

PSipServer::PSipServer(PString& domain, PString& pwd, PString& ip, uint16_t port)
	: m_domain(domain)
	, m_passwd(pwd)
	, m_ip(ip)
	, m_port(port)
{
	std::srand(std::time(NULL));
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
	
	//P_LOG("message:\n%s\n", dest);	
	osip_free(dest);

	if (MSG_IS_REQUEST(sip))
	{
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
	}
	else if (MSG_IS_STATUS_2XX(sip))
	{
		PString callid = sip->call_id->number;
		auto it = m_sipDialog.find(callid);

		if (it != m_sipDialog.end())
		{
			sip_dialog* sd = it->second;
			sd->sipClient->process_sip(sip, sd);
		}
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
	return m_domain;
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
	//P_LOG("message:\n%s\n", dest);

	int ret = sendto(m_sock, dest, length, 0, (struct sockaddr*)&in_addr, in_addrlen);
	if (ret != length)
	{
		P_LOG("send failed");
	}

	osip_free(dest);
	osip_message_free(rsp);
}

void PSipServer::send_sip_rsp(osip_message_t* rsp, const char* ip, const char* port)
{
	sockaddr_in addr;
	socklen_t addrLen = sizeof(addr);
	uint16_t nPort = atoi(port);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);
	addr.sin_port = htons(nPort);

	this->send_sip_rsp(rsp, addr, addrLen);	
}

PMediaClient* PSipServer::CreateClient(PString& url)
{
	if (memcmp(url.c_str(), "gb://", strlen("gb://")))
	{
		P_LOG("wrong gb url:%s", url.c_str());
		return NULL;
	}

	char* p1 = url.data() + strlen("gb://");
	char* p = strchr(p1, '/');

	if (!p)
	{
		P_LOG("wrong url:%s", url.c_str());
		return NULL;
	}

	PString device;

	device.assign(p1, p - p1);

	auto it = m_sipConn.find(device);
	if (it == m_sipConn.end())
	{
		P_LOG("no dev:%s", url.c_str());
		return NULL;
	}

	PString channel = ++p;
	PSipConn* sipConn = it->second;

	return sipConn->init_invite(channel, url);
}

PString& PSipServer::get_ip()
{
	return m_ip;
}

int PSipServer::get_port()
{
	return m_port;
}

void PSipServer::add_dialog(sip_dialog* dialog)
{
	m_sipDialog[dialog->callid] = dialog;
}

void PSipServer::del_dialog(PString& callid)
{
	auto it = m_sipDialog.find(callid);
	if (it != m_sipDialog.end())
	{
		sip_dialog* p = it->second;

		m_sipDialog.erase(it);
		delete p;
	}
}

sip_dialog* PSipServer::get_dialog(PString& callid)
{
	auto it = m_sipDialog.find(callid);
	if (it != m_sipDialog.end())
	{
		return it->second;
	}

	return NULL;
}
