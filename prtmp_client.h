#pragma once

#include "pmedia_client.h"
#include "librtmp/rtmp.h"

class PRtmpClient : public PMediaClient
{
public:
	PRtmpClient(PString& ulr);

	void GetMediaInfo(PRtspConn* conn);
	int on_rtsp_req(PRtspReq& req, PRtspConn* conn);
	void OnRun();

	void OnExit();

	int process_msg(PTaskMsg& msg);

private:
	PString m_url;
	RTMP m_rtmp;
};