#pragma once

#include "pstring.h"
#include <vector>
#include "osipparser2/osip_parser.h"
#include <set>

enum media_pro
{
	RTSP,
	GB28181,
	PRTMP,
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

#define RTP_FLAG_MARKER 0x2 ///< RTP marker bit was set for this packet
#define  RTP_VERSION  2
#define MAX_PAYLOAD_SIZE 1400

#define AV_RB16(x)  ((((const uint8_t*)(x))[0] << 8) | ((const uint8_t*)(x))[1])

#define AV_RB24(x)  ((((const uint8_t*)(x))[0] << 16) | \
    (((const uint8_t*)(x))[1] << 8) | \
    ((const uint8_t*)(x))[2])

#define AV_RB32(x)  ((((const uint8_t*)(x))[0] << 24) | \
    (((const uint8_t*)(x))[1] << 16) | \
    (((const uint8_t*)(x))[2] <<  8) | \
    ((const uint8_t*)(x))[3])

#define AV_RL16(x) ((((const uint8_t*)(x))[1] << 8) | \
((const uint8_t*)(x))[0])

#define AV_RL32(x)                                \
     (((uint32_t)((const uint8_t*)(x))[3] << 24) |    \
                (((const uint8_t*)(x))[2] << 16) |    \
                (((const uint8_t*)(x))[1] <<  8) |    \
                 ((const uint8_t*)(x))[0])

void skip_space(char*& p);

void build_sip_msg(osip_message_t*& sip, sip_dialog* dlg, int cseq, const char* method);

int avio_r8(uint8_t*& pb);

unsigned int avio_rb16(uint8_t*& s);

void avio_skip(uint8_t*& pb, int64_t offset);

unsigned int avio_rb32(uint8_t*& s);

unsigned int avio_rb24(uint8_t*& s);

const uint8_t * ff_avc_find_startcode_internal(
	const uint8_t *p, const uint8_t *end);

const uint8_t * ff_avc_find_startcode(const uint8_t *p, const uint8_t *end);

class PRtspConn;

void parse_send_es(const uint8_t* inbuf, const int insize, int64_t ts,
	int payload, uint8_t* sendbuf, int& seq, std::set<PRtspConn *>& pendPlay);

void nal_send(const uint8_t *buf, int size, int last, uint32_t ts,
	int payload, uint8_t* sendbuf, int& seq, std::set<PRtspConn *>& pendPlay);

void avio_w8(uint8_t*& s, int b);

void avio_wb16(uint8_t*& s, unsigned int val);

void avio_wb32(uint8_t*& s, unsigned int val);


