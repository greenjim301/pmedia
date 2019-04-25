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
	void on_sdp_info(int flv_codecid, int channels, int sample_rate);

private:
	PString m_url;
	RTMP m_rtmp;
	bool m_bfirst;
	char m_sendbuf[2048];
	int m_videoseq;
	bool m_videoready;
	bool m_audioready;
	int m_sdptry;
	int m_audiosample;
	int m_audiochannel;
	int m_audiocodecid;
	int m_audiopayload;
	int m_audioseq;
};