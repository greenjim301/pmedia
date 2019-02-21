#include "prtsp_client.h"
#include "plog.h"
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "prtsp_conn.h"
#include "pmanager.h"
#include "ptask_timer.h"

PRtspClient::PRtspClient(PString& url)
	: m_url(url)
	, m_sock(-1)
	, m_port(0)
	, m_cseq(0)
	, m_tcpOff(0)
	, m_tiemout(0)
	, m_keepMethod(0)
{
	m_playRsp.m_ret = -1;
	m_descbRsp.m_ret = -1;
	m_tcpBuf = (char*)malloc(DEF_BUF_SIZE);
}

PRtspClient::~PRtspClient()
{
	P_LOG("rtsp client release");

	if (m_sock != -1)
	{
		close(m_sock);
	}

	free(m_tcpBuf);
}

int PRtspClient::parse_url(PString& url)
{
	if (0 != memcmp(url.c_str(), "rtsp://", strlen("rtsp://")))
	{
		P_LOG("%s illegal", url.c_str());
		return -1;
	}

	char* start = url.data() + strlen("rtsp://");
	char* p = strchr(start, ':');

	if (p)
	{
		m_ip.assign(start, p - start);
		++p;
		m_port = atoi(p);
	}
	else
	{
		m_port = 554;
		p = strchr(start, '/');

		if (p)
		{
			m_ip.assign(start, p - start);
		}
		else
		{
			m_ip.assign(start, url.size() - (start - url.c_str()));
		}
	}

	return 0;
}

void PRtspClient::get_method(int method, PString& cmd)
{
	switch (method)
	{
	case rtsp_method::OPTIONS:
		cmd = "OPTIONS";
		break;

	case rtsp_method::DESCRIBE:
		cmd = "DESCRIBE";
		break;

	case rtsp_method::SETUP:
		cmd = "SETUP";
		break;

	case rtsp_method::PLAY:
		cmd = "PLAY";
		break;

	case rtsp_method::PAUSE:
		cmd = "PAUSE";
		break;

	case rtsp_method::TEARDOWN:
		cmd = "TEARDOWN";
		break;

	case rtsp_method::SET_PARAMETER:
		cmd = "SET_PARAMETER";
		break;

	case rtsp_method::GET_PARAMETER:
		cmd = "GET_PARAMETER";
		break;

	default:
		break;
	}
}

int PRtspClient::send_rtsp_req(PRtspReq& req)
{
	char sBuf[512];
	char* p = sBuf;

	PString cmd;
	this->get_method(req.m_method, cmd);

	int n = sprintf(p, "%s %s RTSP/1.0\r\n", cmd.c_str(), req.m_url.c_str());	
	p += n;

	n = sprintf(p, "CSeq: %d\r\n",req.m_cseq);
	p += n;

	if (req.m_accept.size())
	{
		n = sprintf(p, "Accept: %s\r\n", req.m_accept.c_str());
		p += n;
	}

	if (req.m_transport.size())
	{
		n = sprintf(p, "Transport: %s\r\n", req.m_transport.c_str());
		p += n;
	}

	if (req.m_session.size())
	{
		n = sprintf(p, "Session: %s\r\n", req.m_session.c_str());
		p += n;
	}

	if (req.m_range.size())
	{
		n = sprintf(p, "Range: %s\r\n", req.m_range.c_str());
		p += n;
	}

	n = sprintf(p, "\r\n");
	p += n;

	int size = p - sBuf;
	p = sBuf;

	P_LOG("\r\n%s", p);

	while (size > 0)
	{
		n = send(m_sock, p, size, 0);
		if (n < 0)
		{
			return -1;
		}
		else
		{
			p += n;
			size -= n;
		}
	}

	m_reqMap[req.m_cseq] = { req.m_method, req.m_url };

	return 0;
}

void PRtspClient::OnRun()
{
	if (this->parse_url(m_url))
	{
		return;
	}

	sockaddr_in sAddr;
	int sAddrLen = sizeof(sockaddr_in);

	memset(&sAddr, 0, sAddrLen);
	sAddr.sin_family = AF_INET;
	sAddr.sin_addr.s_addr = inet_addr(m_ip.c_str());
	sAddr.sin_port = htons(m_port);

	m_sock = socket(AF_INET, SOCK_STREAM, 0);
	
	if (m_sock < 0)
	{
		return ;
	}

	int ret = connect(m_sock, (sockaddr*)&sAddr, sAddrLen);
	if(ret)
	{
		P_LOG("connet %s:%d err:%d", m_ip.c_str(), m_port, errno);

		return ;
	}

	PRtspReq req;

	req.m_method = rtsp_method::OPTIONS;
	req.m_url = m_url;
	req.m_cseq = ++m_cseq;

	if (this->send_rtsp_req(req))
	{
		return;
	}

	this->AddInEvent(m_sock);

	struct epoll_event events[DEF_MAX_EVENTS];
	std::vector<PTaskMsg> msgs;

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
					if (this->tcp_recv())
					{
						P_LOG("client recv err");
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
					return ;
				}
			}
		}
	}
}

void PRtspClient::OnExit()
{
	PManager* pm = PManager::Instance();

	pm->AquireLock();
	pm->UnregistClient(m_url);
	pm->ReleaseLock();

	pm->GetTimer()->UnregistTimer(this);

	PTaskMsg msg(EN_CLIENT_EXIT, NULL);

	for (auto it = m_addConn.begin(); it != m_addConn.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}

	for (auto it = m_pendPlay.begin(); it != m_pendPlay.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}
	
	PMediaClient::OnExit();
}

void PRtspClient::GetMediaInfo(PRtspConn* conn)
{
	if (m_descbRsp.m_ret == 200)
	{
		conn->on_descb_rsp(m_descbRsp);
	}

	conn->AddRef();
	m_addConn.insert(conn);
}

int PRtspClient::tcp_recv()
{
	int ret = recv(m_sock, m_tcpBuf + m_tcpOff, DEF_BUF_SIZE - m_tcpOff, 0);

	if (ret < 0)
	{
		P_LOG("client recv err:%d", errno);
		return -1;
	}
	else if (ret == 0)
	{
		P_LOG("client peer closed");
		return -1;
	}

	m_tcpOff += ret;
	m_tcpBuf[m_tcpOff] = '\0';

	char* dbuf = m_tcpBuf;

	while (m_tcpOff > 0)
	{
		if (*dbuf == '$')
		{
			if (m_tcpOff > 4)
			{
				int16_t len = ntohs(*(int16_t*)(dbuf + 2));
				
				if (m_tcpOff >= len + 4)
				{
					for (auto it = m_pendPlay.begin(); it != m_pendPlay.end(); ++it)
					{
						(*it)->send_tcp_stream(dbuf, len + 4);
					}

					dbuf += len + 4;
					m_tcpOff -= len + 4;

					continue;
				}
				else
				{
					break;
				}
			} 
			else
			{
				break;
			}
		}
		
		if (m_descbRsp.m_ret == 100)
		{
			if (m_tcpOff >= m_descbRsp.m_cntLen)
			{
				ret = this->process_rsp_body(m_descbRsp, dbuf, m_tcpOff);

				if (ret)
				{
					return -1;
				}
			}
			else
			{
				break;
			}
		}

		char* p = strchr(dbuf, '\n');

		if (p)
		{
			PString line;

			if (*(p - 1) == '\r')
			{
				line.assign(dbuf, p - 1 - dbuf);
			}
			else
			{
				line.assign(dbuf, p - dbuf);
			}

			m_tcpOff -= p + 1 - dbuf;
			dbuf = p + 1;

			if (line.size() == 0)
			{
				//收全了
				if (this->parse_process_rsp())
				{
					P_LOG("parse process rsp err");

					return -1;
				}
			}
			else
			{
				P_LOG("%s", line.c_str());

				m_rsp.push_back(line);
			}
		}
		else
		{
			break;//没收全
		}
	}

	if (m_tcpOff != 0)
	{
		memmove(m_tcpBuf, dbuf, m_tcpOff);
	}

	return 0;
}

int PRtspClient::parse_process_rsp()
{
	if (!m_rsp.size())
	{
		P_LOG("rsp null");
		return -1;
	}

	auto it = m_rsp.begin();
	PRtspRsp rsp;

	if (this->parse_rsp_head(*it, rsp))
	{
		goto FAILED;
	}

	for (; it != m_rsp.end(); ++it)
	{
		if (this->parse_line(*it, rsp))
		{
			goto FAILED;
		}
	}

	m_rsp.clear();

	return this->process_rsp(rsp);

FAILED:
	m_rsp.clear();

	return -1;
}

int PRtspClient::parse_rsp_head(PString& str, PRtspRsp& rsp)
{
	char* p = str.data();

	if (0 == memcmp(p, "RTSP/1.0", strlen("RTSP/1.0")))
	{
		p += strlen("RTSP/1.0");
		skip_space(p);
		rsp.m_ret = atoi(p);
	}
	else
	{
		P_LOG("head err");
		return -1;
	}

	return 0;
}

int PRtspClient::parse_line(PString& str, PRtspRsp& rsp)
{
	char* p = str.data();

	if (!memcmp(p, "CSeq:", strlen("CSeq:")))
	{
		p += strlen("CSeq:");
		skip_space(p);
		rsp.m_cseq = atoi(p);
	}
	else if (!memcmp(p, "Public:", strlen("Public:")))
	{
		p += strlen("Public:");
		skip_space(p);
		rsp.m_public = p;
	}
	else if (!memcmp(p, "Content-Base:", strlen("Content-Base:")))
	{
		p += strlen("Content-Base:");
		skip_space(p);
		rsp.m_cntBase = p;
	}
	else if (!memcmp(p, "Content-Length:", strlen("Content-Length:")))
	{
		p += strlen("Content-Length:");
		skip_space(p);
		rsp.m_cntLen = atoi(p);
	}
	else if (!memcmp(p, "Content-Type:", strlen("Content-Type:")))
	{
		p += strlen("Content-Type:");
		skip_space(p);
		rsp.m_cntType = p;
	}
	else if (!memcmp(p, "Session:", strlen("Session:")))
	{
		p += strlen("Session:");
		skip_space(p);
		rsp.m_session = p;
	}
	else if (!memcmp(p, "Transport:", strlen("Transport:")))
	{
		p += strlen("Transport:");
		skip_space(p);
		rsp.m_transport = p;
	}
	else if (!memcmp(p, "Range:", strlen("Range:")))
	{
		p += strlen("Range:");
		skip_space(p);
		rsp.m_range = p;
	}
	else if (!memcmp(p, "RTP-Info:", strlen("RTP-Info:")))
	{
		p += strlen("RTP-Info:");
		skip_space(p);
		rsp.m_rtpinfo = p;
	}

	return 0;
}

int PRtspClient::process_rsp(PRtspRsp& rsp)
{
	auto it = m_reqMap.find(rsp.m_cseq);

	if (it == m_reqMap.end())
	{
		P_LOG("not in map");
		return -1;
	}

	int method = it->second.method;
	PString url = it->second.url;

	m_reqMap.erase(it);

	if (rsp.m_ret != 200)
	{
		P_LOG("ret err:%d", rsp.m_ret);
		return -1;
	}

	switch (method)
	{
	case rtsp_method::OPTIONS:
	{
		char* p = strstr(rsp.m_public.data(), "GET_PARAMETER");
		if (p)
		{
			m_keepMethod = rtsp_method::GET_PARAMETER;
		} 
		else
		{
			p = strstr(rsp.m_public.data(), "SET_PARAMETER");
			if (p)
			{
				m_keepMethod = rtsp_method::SET_PARAMETER;
			}
		}

		PRtspReq req;

		req.m_method = rtsp_method::DESCRIBE;
		req.m_cseq = ++m_cseq;
		req.m_accept = "application/sdp";
		req.m_url = m_url;

		return this->send_rtsp_req(req);
	}
		break;

	case rtsp_method::DESCRIBE:
	{
		if (rsp.m_cntLen > 0)
		{
			m_descbRsp = rsp;
			m_descbRsp.m_ret = 100;
		}

		return 0;
	}
	break;

	case rtsp_method::SETUP:
	{
		if (!m_tiemout)
		{
			char* p = strstr(rsp.m_session.data(), "timeout=");
			if (p)
			{
				p += strlen("timeout=");
				m_tiemout = atoi(p);
			}
		}		

		m_setupRsp[url] = rsp;

		auto itPend = m_pendSetup.find(url);

		if (itPend != m_pendSetup.end())
		{
			std::set<PRtspConn*>& connVec = itPend->second;

			if (connVec.empty())
			{
				return -1;
			}
			
			for (auto it = connVec.begin(); it != connVec.end(); ++it)
			{
				(*it)->on_rtsp_rsp(rsp);
			}

			m_pendSetup.erase(itPend);

			return 0;
		}
		else
		{
			return -1;
		}
	}
	break;

	case rtsp_method::PLAY:
	{
		if (m_pendPlay.empty())
		{
			return -1;
		}

		m_playRsp = rsp;

		for (auto it = m_pendPlay.begin(); it != m_pendPlay.end(); ++it)
		{
			(*it)->on_rtsp_rsp(rsp);
		}

		if (m_tiemout && m_keepMethod)
		{
			PManager::Instance()->GetTimer()->RegistTimer(this, m_tiemout, true);
		}

		return 0;
	}
	break;

	case rtsp_method::GET_PARAMETER:
	case rtsp_method::SET_PARAMETER:
		return 0;
		break;

	default:
		break;
	}

	P_LOG("method err:%d", method);

	return -1;
}

int PRtspClient::process_rsp_body(PRtspRsp& rsp, char*& buf, int& bufSize)
{
	char c = buf[rsp.m_cntLen];

	buf[rsp.m_cntLen] = '\0';

	char* dbuf = buf;
	PString line;	
	std::vector<PString>& sdp = rsp.m_sdp;

	char* p = strchr(dbuf, '\n');

	while (p && *p != '\0')
	{
		if (*(p - 1) == '\r')
		{
			line.assign(dbuf, p - 1 - dbuf);
		}
		else
		{
			line.assign(dbuf, p - dbuf);
		}

		P_LOG("%s", line.c_str());

		sdp.push_back(line);

		dbuf = p + 1;
		p = strchr(dbuf, '\n');
	}

	buf[rsp.m_cntLen] = c;

	buf += rsp.m_cntLen;
	bufSize -= rsp.m_cntLen;

	rsp.m_ret = 200;

	for (auto it = m_addConn.begin(); it != m_addConn.end(); ++it)
	{
		(*it)->on_descb_rsp(rsp);
	}

	return 0;
}

int PRtspClient::on_rtsp_req(PRtspReq& req, PRtspConn* conn)
{
	switch (req.m_method)
	{
	case rtsp_method::SETUP:
	{
		auto it = m_setupRsp.find(req.m_url);

		if (it != m_setupRsp.end())//url setupd
		{
			PRtspRsp& rsp = it->second;

			return conn->on_rtsp_rsp(rsp);
		} 
		else
		{
			auto itPend = m_pendSetup.find(req.m_url);

			if (itPend == m_pendSetup.end())
			{
				req.m_cseq = ++m_cseq;

				if(this->send_rtsp_req(req))
				{
					return -1;
				}
				else
				{
					std::set<PRtspConn*> connVec;

					connVec.insert(conn);
					m_pendSetup[req.m_url] = connVec;
				}
			} 
			else
			{
				itPend->second.insert(conn);
			}			
		}
	}
		break;

	case rtsp_method::PLAY:
	{
		if (m_playRsp.m_ret != -1)//url played
		{
			conn->on_rtsp_rsp(m_playRsp);
			
			m_addConn.erase(conn);
			m_pendPlay.insert(conn);

			return 0;
		}
		else
		{
			if (m_pendPlay.empty())
			{
				req.m_cseq = ++m_cseq;

				if (this->send_rtsp_req(req))
				{
					return -1;
				}
				else
				{
					m_addConn.erase(conn);
					m_pendPlay.insert(conn);
				}
			}
			else
			{
				m_addConn.erase(conn);
				m_pendPlay.insert(conn);
			}
		}
	}
	break;

	default:
		break;
	}

	return 0;
}

int PRtspClient::process_msg(PTaskMsg& msg)
{
	switch (msg.m_type)
	{
	case EN_CONN_EXIT:
	{
		PRtspConn* conn = (PRtspConn*)msg.m_body;

		for (auto it = m_pendSetup.begin(); it != m_pendSetup.end(); ++it)
		{
			it->second.erase(conn);			
		}
		
		m_pendPlay.erase(conn);
		m_addConn.erase(conn);	

		conn->DelRef();

		if (m_addConn.empty() && m_pendPlay.empty())
		{
			P_LOG("conn empty");
			return -1;
		}
		else
		{
			return 0;
		}		
	}
	break;

	case EN_TASK_TIMER:
	{
		PRtspReq req;

		req.m_method = m_keepMethod;
		req.m_url = m_url;
		req.m_cseq = ++m_cseq;
		req.m_session = m_playRsp.m_session;
		
		return this->send_rtsp_req(req);
	}
	break;

	default:
		break;
	}

	return 0;
}
