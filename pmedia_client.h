#pragma once

#include "ptask.h"
#include "pstring.h"
#include "prtsp_comm.h"

class PRtspConn;
class PRtspReq;

class PMediaClient : public PTask
{
public:
	virtual void OnExit();

	virtual void GetMediaInfo(PRtspConn* conn);

	virtual int on_rtsp_req(PRtspReq& req, PRtspConn* conn) = 0;

protected:
	std::set<PRtspConn*>  m_addConn;
	std::set<PRtspConn*>  m_pendPlay;
	PRtspRsp  m_descbRsp;
};


