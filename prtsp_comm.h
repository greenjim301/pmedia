#pragma once

#include "pstring.h"
#include <vector>
#include "osipparser2/osip_parser.h"

enum media_pro
{
	RTSP,
	GB28181,
};

enum rtsp_method
{
	OPTIONS = 0,
	DESCRIBE = 1,
	SETUP = 3,
	PLAY = 5,
	PAUSE = 7,
	TEARDOWN = 9,
	SET_PARAMETER = 11,
	GET_PARAMETER = 13,
};

enum
{
	EN_DESCB_REQ = 1,
	EN_DESCB_RSP,
	EN_SETUP_REQ,
	EN_SETUP_RSP,
	EN_PLAY_REQ,
	EN_PLAY_RSP,
	EN_PAUSE_REQ,
	EN_PAUSE_RSP,
	EN_TEARDOWN_REQ,
	EN_TEARDOWN_RSP,
	EN_SETPARAM_REQ,
	EN_SETPARAM_RSP,
	EN_GETPARAM_REQ,
	EN_GETPARAM_RSP,
	EN_TASK_TIMER,
	EN_CONN_EXIT,
	EN_CLIENT_EXIT,
};

class PRtspReq
{
public:
	int m_method;
	PString m_url;
	PString m_version;
	int m_cseq;
	PString m_accept;
	PString m_transport;
	PString m_session;
	PString m_range;

	int m_pro;
	PString m_proUrl;
};

class PRtspRsp
{
public:
	PRtspRsp() : m_ret(0), m_cntLen(0){}

	int m_ret;
	int m_cseq;
	PString m_descb;
	PString m_public;
	PString m_cntBase;
	PString m_cntType;
	PString m_session;
	PString m_transport;
	PString m_range;
	PString m_rtpinfo;
	int m_cntLen;

	std::vector<PString> m_sdp;
};

class PSipClient;

struct sip_dialog
{
	PString callid;
	PString destUser;
	PString destHost;
	PString destPort;
	PString localContact;
	PString via;
	PString localTag;
	PString remoteTag;
	PSipClient* sipClient;
	int cseq;
};

#define  DEF_BUF_SIZE   512 * 1024

void skip_space(char*& p);

void build_sip_msg(osip_message_t*& sip, sip_dialog* dlg, int cseq, const char* method);