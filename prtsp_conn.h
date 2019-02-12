#pragma once

#include "ptask.h"
#include "pstring.h"
#include "prtsp_comm.h"

class PRtspClient;

class PRtspConn : public PTask
{
public:
	PRtspConn(int fd);
	~PRtspConn();

	void OnRun();
	void OnExit();

	int on_descb_rsp(PRtspRsp& rsp);
	int on_rtsp_rsp(PRtspRsp& rsp);
	int send_tcp_stream(char* buf, int size);

private:
	enum media_pro
	{
		RTSP,
	};

private:
	int tcp_recv();

	int parse_process_req();

	void skip_to_space(char*& p);

	int parse_method(char*& p, PRtspReq& req);
	void parse_url_version(char*& p, PRtspReq& req);
	int parse_req_head(PString& str, PRtspReq& req);
	int parse_line(PString& str, PRtspReq& req);
	int process_req(PRtspReq& req);
	int get_pro_url(PRtspReq& req);
	int process_msg(PTaskMsg& msg);

	void get_descb(int ret, PString& descb);
	int send_rtsp_rsp(PRtspRsp& rsp);

private:
	int m_sock;
	char* m_tcpBuf;
	int m_tcpOff;
	int m_lastCSeq;
	PString m_preUrl;
	PRtspClient* m_rtspClient;

	std::vector<PString> m_req;
};
