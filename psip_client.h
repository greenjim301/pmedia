#pragma once

#include "pmedia_client.h"
#include "pstring.h"
#include "prtsp_comm.h"
#include <set>
#include "osipparser2/osip_parser.h"

class PRtspConn;
class PSipServer;
struct sip_dialog;

class PSipClient : public PMediaClient
{
public:
	PSipClient(int sock, PString& callid, PString& url, PSipServer* server);
	~PSipClient();

	void OnRun();
	void OnExit();

	void GetMediaInfo(PRtspConn* conn);
	int on_rtsp_req(PRtspReq& req, PRtspConn* conn);

	void process_sip(osip_message_t* sip, sip_dialog* dlg);

private:
	int udp_recv();
	int process_msg(PTaskMsg& msg);

private:
	int m_sock;
	PString m_callid;
	PString m_url;
	PSipServer* m_server;
	std::set<PRtspConn*>  m_addConn;
	std::set<PRtspConn*>  m_pendPlay;
	PRtspRsp  m_descbRsp;
};