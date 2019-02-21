#include "psip_client.h"
#include "prtsp_conn.h"
#include "pmanager.h"
#include "psip_server.h"
#include "plog.h"
#include <unistd.h>

PSipClient::PSipClient(int sock, PString& callid, PString& url, PSipServer* server)
	: m_sock(sock)
	, m_callid(callid)
	, m_url(url)
	, m_server(server)
{

}

PSipClient::~PSipClient()
{
	P_LOG("sip client release");
	close(m_sock);
}

void PSipClient::OnRun()
{
	this->AddInEvent(m_sock);

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

int PSipClient::udp_recv()
{
	sockaddr_in cAddr;
	socklen_t cAddrLen = sizeof(sockaddr_in);
	char buf[2048];

	int ret = recvfrom(m_sock, buf, 2048, 0, (struct sockaddr *)&cAddr, &cAddrLen);

	if (ret < 0)
	{
		return ret;
	}

	return 0;
}

void PSipClient::OnExit()
{
	PManager* pm = PManager::Instance();

	pm->AquireLock();
	pm->UnregistClient(m_url);
	pm->ReleaseLock();

	m_server->del_dialog(m_callid);

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

void PSipClient::GetMediaInfo(PRtspConn* conn)
{
	if (m_descbRsp.m_ret == 200)
	{
		conn->on_descb_rsp(m_descbRsp);
	}

	conn->AddRef();
	m_addConn.insert(conn);
}

int PSipClient::on_rtsp_req(PRtspReq& req, PRtspConn* conn)
{
	return -1;
}

void PSipClient::process_sip(osip_message_t* rsp, sip_dialog* dlg)
{
	if (rsp->contacts.nb_elt)
	{
		osip_contact_t* ct;
		osip_message_get_contact(rsp, 0, &ct);

		dlg->destUser = ct->url->username;
		dlg->destHost = ct->url->host;
		dlg->destPort = ct->url->port;
	}

	char* dest;
	osip_to_to_str(rsp->to, &dest);
	dlg->remoteTag = dest;
	osip_free(dest);

	osip_message_t* sip;
	int cseq = atoi(rsp->cseq->number);
	char temp[32];

	sprintf(temp, "z9hG4bK%d", std::rand());
	dlg->branch = temp;

	build_sip_msg(sip, dlg, cseq, "ACK");

	m_server->send_sip_rsp(sip, dlg->destHost.c_str(), dlg->destPort.c_str());
}

int PSipClient::process_msg(PTaskMsg& msg)
{
	switch (msg.m_type)
	{
	case EN_CONN_EXIT:
	{
		PRtspConn* conn = (PRtspConn*)msg.m_body;

		m_pendPlay.erase(conn);
		m_addConn.erase(conn);

		conn->DelRef();

		if (m_addConn.empty() && m_pendPlay.empty())
		{
			P_LOG("sip conn empty");

			sip_dialog* dlg = m_server->get_dialog(m_callid);

			if (dlg)
			{
				osip_message_t* sip;
				char temp[32];

				sprintf(temp, "z9hG4bK%d", std::rand());
				dlg->branch = temp;

				build_sip_msg(sip, dlg, ++dlg->cseq, "BYE");		

				m_server->send_sip_rsp(sip, dlg->destHost.c_str(), dlg->destPort.c_str());
			}

			return -1;
		}
	}
	break;

	default:
		break;
	}

	return 0;
}

