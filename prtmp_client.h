#pragma once

#include "pmedia_client.h"
#include "librtmp/rtmp.h"

class PRtmpClient : public PMediaClient
{
public:
	PRtmpClient(PString& ulr);

	int on_rtsp_req(PRtspReq& req, PRtspConn* conn);
	void OnRun();

	void OnExit();

	int process_msg(PTaskMsg& msg);

private:
	void parse_flv(uint8_t* inbuf, int insize, int& inoffset);
	void on_sdp_info(int nAudio);

private:
	PString m_url;
	RTMP m_rtmp;
	bool m_bfirst;
	int m_nAudio;
	char m_sendbuf[2048];
	int m_videoseq;
};