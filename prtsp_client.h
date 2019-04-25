#pragma once

#include "pmedia_client.h"
#include "pstring.h"
#include "prtsp_comm.h"
#include <map>
#include <set>

class PRtspConn;

class PRtspClient : public PMediaClient
{
public:
	PRtspClient(PString& url);
	~PRtspClient();

	void OnRun();
	void OnExit();
	
	int on_rtsp_req(PRtspReq& req, PRtspConn* conn);

private:
	int parse_url(PString& url);
	void get_method(int method, PString& cmd);
	int tcp_recv();
	int send_rtsp_req(PRtspReq& req);

	int parse_rsp_head(PString& str, PRtspRsp& rsp);
	int parse_line(PString& str, PRtspRsp& rsp);
	int process_rsp(PRtspRsp& rsp);
	int parse_process_rsp();
	int process_rsp_body(PRtspRsp& rsp, char*& buf, int& bufSize);
	int process_msg(PTaskMsg& msg);

private:
	PString m_url;
	PString m_ip;
	uint16_t m_port;
	int m_sock;
	uint32_t m_cseq;

	char* m_tcpBuf;
	int m_tcpOff;
	std::vector<PString> m_rsp;

	struct SRtspReq
	{
		int method;
		PString url;
	};

	std::map<uint32_t, SRtspReq> m_reqMap;
	std::map<PString, PRtspRsp> m_setupRsp;
	std::map<PString, std::set<PRtspConn*> > m_pendSetup;
	
	PRtspRsp  m_playRsp;
	int m_keepMethod;
	int m_tiemout;
};
