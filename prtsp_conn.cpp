#include "prtsp_conn.h"
#include "plog.h"
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "pmanager.h"
#include "pmedia_client.h"

PRtspConn::PRtspConn(int fd)
	: m_sock(fd)
	, m_tcpOff(0)
	, m_meidaClient(NULL)
{
	m_tcpBuf = (char*)malloc(DEF_BUF_SIZE);
}

PRtspConn::~PRtspConn()
{
	P_LOG("rtsp conn release");

	close(m_sock);
	free(m_tcpBuf);
}

void PRtspConn::OnRun()
{
	if (this->AddInEvent(m_sock))
	{
		P_LOG("add tcp in event err");
		return;
	}

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
					if (this->tcp_recv())
					{
						P_LOG("conn recv err");
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

void PRtspConn::OnExit()
{
	if (m_meidaClient)
	{
		PTaskMsg msg(EN_CONN_EXIT, this);
		m_meidaClient->EnqueMsg(msg);

		m_meidaClient->DelRef();
	}

	PTask::OnExit();
}

int PRtspConn::tcp_recv()
{
	int ret = recv(m_sock, m_tcpBuf + m_tcpOff, DEF_BUF_SIZE - m_tcpOff, 0);
	
	if (ret < 0)
	{
		P_LOG("rtsp conn recv err:%d", errno);
		return -1;
	}
	else if (ret == 0)
	{
		P_LOG("tcp peer closed");
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
					//disgard

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

			if ( line.size() == 0 )
			{
				//收全了
				if (this->parse_process_req())
				{
					P_LOG("parse process req err");

					return -1;
				}
			} 
			else
			{
				m_req.push_back(line);
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

int PRtspConn::parse_method(char*& p, PRtspReq& req)
{
	if (!memcmp(p, "OPTIONS", strlen("OPTIONS")))
	{
		req.m_method = rtsp_method::OPTIONS;
		p += strlen("OPTIONS");
	}
	else if (!memcmp(p, "DESCRIBE", strlen("DESCRIBE")))
	{
		req.m_method = rtsp_method::DESCRIBE;
		p += strlen("DESCRIBE");
	}
	else if (!memcmp(p, "SETUP", strlen("SETUP")))
	{
		req.m_method = rtsp_method::SETUP;
		p += strlen("SETUP");
	}
	else if (!memcmp(p, "PLAY", strlen("PLAY")))
	{
		req.m_method = rtsp_method::PLAY;
		p += strlen("PLAY");
	}
	else if (!memcmp(p, "PAUSE", strlen("PAUSE")))
	{
		req.m_method = rtsp_method::PAUSE;
		p += strlen("PAUSE");
	}
	else if (!memcmp(p, "TEARDOWN", strlen("TEARDOWN")))
	{
		req.m_method = rtsp_method::TEARDOWN;
		p += strlen("TEARDOWN");
	}
	else if (!memcmp(p, "SET_PARAMETER", strlen("SET_PARAMETER")))
	{
		req.m_method = rtsp_method::SET_PARAMETER;
		p += strlen("SET_PARAMETER");
	}
	else if (!memcmp(p, "GET_PARAMETER", strlen("GET_PARAMETER")))
	{
		req.m_method = rtsp_method::GET_PARAMETER;
		p += strlen("GET_PARAMETER");
	}
	else
	{
		return -1;
	}

	return 0;
}

void PRtspConn::skip_to_space(char*& p)
{
	while (*p != '\0' && *p != ' ')
	{
		++p;
	}
}

void PRtspConn::parse_url_version(char*& p, PRtspReq& req)
{
	skip_space(p);

	char* url = p;

	this->skip_to_space(p);

	req.m_url.assign(url, p - url);

	skip_space(p);

	req.m_version = p;
}

int PRtspConn::parse_req_head(PString& str, PRtspReq& req)
{
	char* p = str.data();

	if (this->parse_method(p, req))
	{
		return -1;
	}

	this->parse_url_version(p, req);

	return 0;
}

int PRtspConn::parse_line(PString& str, PRtspReq& req)
{
	char* p = str.data();

	if (!memcmp(p, "CSeq:", strlen("CSeq:")))
	{
		p += strlen("CSeq:");
		skip_space(p);
		req.m_cseq = atoi(p);
	}
	else if (!memcmp(p, "Transport:", strlen("Transport:")))
	{
		p += strlen("Transport:");
		skip_space(p);
		req.m_transport = p;
	}
	else if (!memcmp(p, "Session:", strlen("Session:")))
	{
		p += strlen("Session:");
		skip_space(p);
		req.m_session = p;
	}
	else if (!memcmp(p, "Range:", strlen("Range:")))
	{
		p += strlen("Range:");
		skip_space(p);
		req.m_range = p;
	}

	return 0;
}

int PRtspConn::get_pro_url(PRtspReq& req)
{
	char* p = req.m_url.data();

	if (memcmp(p, "rtsp://", strlen("rtsp://")))
	{
		return -1;
	}

	p += strlen("rtsp://");

	char* p1 = strstr(p, "://");
	if (!p1)
	{
		return -1;
	}

	char* p2 = p1 - 1;
	while (*p2 != '/')
	{
		--p2;
	}

	m_preUrl.assign(req.m_url.c_str(), p2 - req.m_url.c_str());

	req.m_proUrl = ++p2;

	PString pro;

	pro.assign(p2, p1 - p2);

	if (!memcmp(pro.c_str(), "rtsp", strlen("rtsp")))
	{
		req.m_pro = media_pro::RTSP;
	}
	else if (!memcmp(pro.c_str(), "gb", strlen("gb")))
	{
		req.m_pro = media_pro::GB28181;
	}

	return 0;
}

int PRtspConn::on_descb_rsp(PRtspRsp& in_rsp)
{
	PRtspRsp rsp = in_rsp;
	PString cntBase = m_preUrl;

	if (rsp.m_cntBase.size())
	{
		cntBase.append("/", 1);
		cntBase.append(rsp.m_cntBase.c_str(), rsp.m_cntBase.size());
	}

	rsp.m_cseq = m_lastCSeq;
	rsp.m_cntBase = cntBase;

	return this->send_rtsp_rsp(rsp);
}

int PRtspConn::on_rtsp_rsp(PRtspRsp& rsp)
{
	rsp.m_cseq = m_lastCSeq;

	return this->send_rtsp_rsp(rsp);
}

int PRtspConn::process_msg(PTaskMsg& msg)
{
	switch (msg.m_type)
	{
	case EN_CLIENT_EXIT:
	{
		P_LOG("client exit");
		return -1;
	}
	break;

	default:
		break;
	}

	return 0;
}

void PRtspConn::get_descb(int ret, PString& descb)
{
	switch (ret)
	{
	case 200:
		descb = "OK";
		break;

	default:
		break;
	}
}

int PRtspConn::send_rtsp_rsp(PRtspRsp& rsp)
{
	this->get_descb(rsp.m_ret, rsp.m_descb);

	char sbuf[2048];
	char sdpBuf[1024];
	char* p = sbuf;

	int n = sprintf(p, "RTSP/1.0 %d %s\r\n", rsp.m_ret, rsp.m_descb.c_str());
	p += n;

	n = sprintf(p, "CSeq: %d\r\n", rsp.m_cseq);
	p += n;

	if (rsp.m_public.size())
	{
		n = sprintf(p, "Public: %s\r\n", rsp.m_public.c_str());
		p += n;
	}

	if (rsp.m_cntBase.size())
	{
		n = sprintf(p, "Content-Base: %s\r\n", rsp.m_cntBase.c_str());
		p += n;
	}

	if (rsp.m_cntType.size())
	{
		n = sprintf(p, "Content-Type: %s\r\n", rsp.m_cntType.c_str());
		p += n;
	}

	if (rsp.m_session.size())
	{
		n = sprintf(p, "Session: %s\r\n", rsp.m_session.c_str());
		p += n;
	}

	if (rsp.m_transport.size())
	{
		n = sprintf(p, "Transport: %s\r\n", rsp.m_transport.c_str());
		p += n;
	}

	if (rsp.m_range.size())
	{
		n = sprintf(p, "Range: %s\r\n", rsp.m_range.c_str());
		p += n;
	}

	if (rsp.m_rtpinfo.size())
	{
		n = sprintf(p, "RTP-Info: %s\r\n", rsp.m_rtpinfo.c_str());
		p += n;
	}

	rsp.m_cntLen = 0;

	if (rsp.m_sdp.size())
	{
		char* pp = sdpBuf;
		for (auto it = rsp.m_sdp.begin(); it != rsp.m_sdp.end(); ++it)
		{
			n = sprintf(pp, "%s\r\n", it->c_str());
			pp += n;
		}
		
		rsp.m_cntLen = pp - sdpBuf;
	}

	if (rsp.m_cntLen > 0)
	{
		n = sprintf(p, "Content-Length: %d\r\n", rsp.m_cntLen);
		p += n;
	}
	
	n = sprintf(p, "\r\n");
	p += n;

	if (rsp.m_cntLen > 0)
	{
		memcpy(p, sdpBuf, rsp.m_cntLen);
		p += rsp.m_cntLen;
	}

	int size = p - sbuf;
	p = sbuf;

	return this->send_tcp_stream(p, size);
}

int PRtspConn::send_tcp_stream(char* p, int size)
{
	int n;

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

	return 0;
}

int PRtspConn::process_req(PRtspReq& req)
{
	m_lastCSeq = req.m_cseq;

	switch (req.m_method)
	{
	case rtsp_method::OPTIONS:
	{
		PRtspRsp rsp;

		rsp.m_ret = 200;
		rsp.m_cseq = req.m_cseq;
		rsp.m_public = "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, SET_PARAMETER, GET_PARAMETER";

		return this->send_rtsp_rsp(rsp);
	}
		break;

	case rtsp_method::DESCRIBE:
	{
		this->get_pro_url(req);

		PManager* manager = PManager::Instance();

		manager->AquireLock();

		PMediaClient* client = manager->GetMediaClient(this, req.m_pro, req.m_proUrl);
		
		if (!client)
		{
			P_LOG("get client err:%s", req.m_proUrl.c_str());
			manager->ReleaseLock();
			return -1;
		}

		client->AddRef();

		manager->ReleaseLock();

		m_meidaClient = client;
	}
		break;

	case rtsp_method::SETUP:
	case rtsp_method::PLAY:
	{
		this->get_pro_url(req);

		req.m_url = req.m_proUrl;

		return m_meidaClient->on_rtsp_req(req, this);
	}
		break;

	case rtsp_method::TEARDOWN:
	case rtsp_method::GET_PARAMETER:
	case rtsp_method::SET_PARAMETER:
	{
		PRtspRsp rsp;

		rsp.m_ret = 200;
		rsp.m_cseq = req.m_cseq;
		rsp.m_session = req.m_session;

		return this->send_rtsp_rsp(rsp);
	}
	break;

	default:
		break;
	}

	return 0;
}

int PRtspConn::parse_process_req()
{
	if (!m_req.size())
	{
		return -1;
	}

	auto it = m_req.begin();
	PRtspReq req;

	if (this->parse_req_head(*it, req))
	{
		goto FAILED;
	}

	for (; it != m_req.end(); ++it)
	{
		if (this->parse_line(*it, req))
		{
			goto FAILED;
		}
	}

	m_req.clear();

	return this->process_req(req);

FAILED:
	m_req.clear();

	return -1;
}
