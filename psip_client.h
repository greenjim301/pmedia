#pragma once

#include "pmedia_client.h"
#include "pstring.h"
#include "prtsp_comm.h"
#include <set>
#include "osipparser2/osip_parser.h"
#include <map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <list>

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

	int on_rtsp_req(PRtspReq& req, PRtspConn* conn);

	void process_sip(osip_message_t* sip, sip_dialog* dlg);

	struct es_info
	{
		int es_id;
		int type;
	};

	void on_es_info(std::list<es_info>& es_info_lst);

private:
	int udp_recv();
	int process_msg(PTaskMsg& msg);
	void process_pend_rtp();
	void process_cur_rtp();
	void nal_send(const uint8_t *buf, int size, int last, uint32_t ts);

private:
	int m_sock;
	PString m_callid;
	PString m_url;
	PSipServer* m_server;

	sockaddr_in m_cAddr;
	socklen_t m_cAddrLen;
	char m_buf[2048];
	int m_payload_type;

	bool m_first;
	uint16_t m_exp_seq;

	class RtpPack
	{
	public:
		RtpPack();
		RtpPack(uint16_t iseq, int iflags, uint16_t its, uint8_t* ibuf, int ilen);
		RtpPack(const RtpPack& rPack);
		
		uint16_t seq;
		uint32_t timestamp;
		int flags;
		uint8_t buf[64 * 24];
		int len;
	};

	std::vector<RtpPack> m_curRtpPacks;
	std::map<uint16_t, RtpPack> m_pendRtpPacks;

	uint8_t* m_packBuf;
	uint8_t* m_psBuf;
	int m_psoff;
	int64_t m_last_ts;

	char m_sendBuf[2048];
	int m_audio_payload;
	int m_audioSeq;
	int m_videoSeq;
};