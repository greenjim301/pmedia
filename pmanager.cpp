#include "pmanager.h"
#include "prtsp_client.h"
#include "psip_server.h"
#include "plog.h"
#include <unistd.h>
#include "pmedia_client.h"

PManager* PManager::m_manager = NULL;
pthread_mutex_t PManager::m_mutex = PTHREAD_MUTEX_INITIALIZER;

PManager* PManager::Instance()
{
	if (PManager::m_manager)
	{
		return PManager::m_manager;
	}
	else
	{
		pthread_mutex_lock(&PManager::m_mutex);

		if (PManager::m_manager)
		{
			pthread_mutex_unlock(&PManager::m_mutex);
			return PManager::m_manager;
		}
		else
		{
			PManager::m_manager = new PManager;
			pthread_mutex_unlock(&PManager::m_mutex);
			return PManager::m_manager;
		}
	}
}

PManager::PManager()
	: m_timer(NULL)
	, m_sipServer(NULL)
	, m_rtpIP("192.168.1.155")
	, m_minPort(10000)
	, m_maxPort(20000)
{
	m_curPort = m_minPort;
}

void PManager::AquireLock()
{
	pthread_mutex_lock(&PManager::m_mutex);
}

PMediaClient* PManager::GetMediaClient(PRtspConn* rtspConn, int pro, PString& url)
{
	PMediaClient* cl = NULL;
	auto it = m_urlClient.find(url);

	if (it != m_urlClient.end())
	{
		cl = it->second;
		cl->GetMediaInfo(rtspConn);

		return cl;
	}
	else
	{
		if (pro == media_pro::RTSP)
		{
			cl = new PRtspClient(url);
			
			cl->Start();
			this->RegistClient(url, cl);
			cl->GetMediaInfo(rtspConn);
		}
		else if (pro == media_pro::GB28181)
		{
			cl = m_sipServer->CreateClient( url);
			
			if (cl)
			{
				this->RegistClient(url, cl);
				cl->GetMediaInfo(rtspConn);
			}
		}
	}

	return cl;
}

void PManager::RegistClient(PString& url, PMediaClient* task)
{
	m_urlClient[url] = task;
}

void PManager::UnregistClient(PString& url)
{
	m_urlClient.erase(url);
}

void PManager::ReleaseLock()
{
	pthread_mutex_unlock(&PManager::m_mutex);
}

void PManager::SetTimer(PTaskTimer* timer)
{
	m_timer = timer;
}

PTaskTimer* PManager::GetTimer()
{
	return m_timer;
}

void PManager::SetSipServer(PSipServer* s)
{
	m_sipServer = s;
}

int PManager::CreateUdpSock(int& out_sock, PString& out_ip, uint16_t& out_port)
{
	int retry = 1000;
	
	out_ip = m_rtpIP;

	while (retry)
	{
		--retry;
		out_sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (out_sock < 0)
		{
			P_LOG("create udp sock err:%d", errno);
			return -1;
		}

		struct sockaddr_in bind_addr;

		memset(&bind_addr, 0, sizeof(bind_addr));
		bind_addr.sin_family = AF_INET;
		bind_addr.sin_addr.s_addr = inet_addr(m_rtpIP.c_str());
		bind_addr.sin_port = htons(m_curPort);

		if (bind(out_sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr)))
		{
			close(out_sock);

			++m_curPort;
			if (m_curPort > m_maxPort)
			{
				m_curPort = m_minPort;
			}
		}
		else
		{
			out_port = m_curPort;
			P_LOG("alloc udp port:%d", out_port);

			++m_curPort;
			if (m_curPort > m_maxPort)
			{
				m_curPort = m_minPort;
			}

			return 0;
		}
	}

	return -1;
}

