#include "psip_client.h"
#include "prtsp_conn.h"
#include "pmanager.h"
#include "psip_server.h"
#include "plog.h"
#include <unistd.h>
#include "osipparser2/sdp_message.h"

#define RTP_FLAG_MARKER 0x2 ///< RTP marker bit was set for this packet

#define AV_RB16(x)  ((((const uint8_t*)(x))[0] << 8) | ((const uint8_t*)(x))[1])
#define AV_RB32(x)  ((((const uint8_t*)(x))[0] << 24) | \
    (((const uint8_t*)(x))[1] << 16) | \
    (((const uint8_t*)(x))[2] <<  8) | \
    ((const uint8_t*)(x))[3])

#define AV_NOPTS_VALUE          ((int64_t)UINT64_C(0x8000000000000000))

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)

/* mpeg2 */
#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf

PSipClient::PSipClient(int sock, PString& callid, PString& url, PSipServer* server)
	: m_sock(sock)
	, m_callid(callid)
	, m_url(url)
	, m_server(server)
	, m_first(true)
{
	m_packBuf = (uint8_t*)malloc(1024 * 1024);
}

PSipClient::~PSipClient()
{
	P_LOG("sip client release");
	close(m_sock);
	free(m_packBuf);
}

void PSipClient::OnRun()
{
	this->AddInEvent(m_sock);

	struct epoll_event events[DEF_MAX_EVENTS];
	std::vector<PTaskMsg> msgs;
	int ret;

	for (;;)
	{
		ret = this->WaitEvent(events, msgs, -1);

		if (ret < 0)
		{
			P_LOG("epoll wait err:%d", errno);
			return;
		}
		else
		{
			for (int i = 0; i < ret; ++i)
			{
				struct epoll_event& ev = events[i];

				if (ev.data.fd == m_sock)
				{
					if (this->udp_recv())
					{
						P_LOG("sip recv err");
						return;
					}
				}
				else
				{
					P_LOG("epoll no match fd");
					return;
				}
			}

			for (auto it = msgs.begin(); it != msgs.end(); ++it)
			{
				if (this->process_msg(*it))
				{
					return;
				}
			}

		}
	}
}

int PSipClient::udp_recv()
{
	m_cAddrLen = sizeof(m_cAddr);

	int ret = recvfrom(m_sock, m_buf, 2048, 0, (struct sockaddr *)&m_cAddr, &m_cAddrLen);

	if (ret <= 0)
	{
		return ret;
	}

	uint8_t* buf = (uint8_t*)m_buf;
	int len = ret;

	unsigned int ssrc;
	int payload_type, flags = 0;
	uint16_t seq;
	int ext, csrc;
	uint32_t timestamp;
	int rv = 0;

	csrc = buf[0] & 0x0f;
	ext = buf[0] & 0x10;
	payload_type = buf[1] & 0x7f;
	if (buf[1] & 0x80)
		flags |= RTP_FLAG_MARKER;
	seq = AV_RB16(buf + 2);
	timestamp = AV_RB32(buf + 4);
	ssrc = AV_RB32(buf + 8);

	if (buf[0] & 0x20) {
		int padding = buf[len - 1];
		if (len >= 12 + padding)
			len -= padding;
	}

	len -= 12;
	buf += 12;

	len -= 4 * csrc;
	buf += 4 * csrc;
	if (len < 0)
	{
		P_LOG("rtp len err");
		return 0;
	}

	/* RFC 3550 Section 5.3.1 RTP Header Extension handling */
	if (ext) {
		if (len < 4)
		{
			P_LOG("rtp len err");
			return 0;
		}
			
		/* calculate the header extension length (stored as number
		* of 32-bit words) */
		ext = (AV_RB16(buf + 2) + 1) << 2;

		if (len < ext)
		{
			P_LOG("rtp len err");
			return 0;
		}
		// skip past RTP header extension
		len -= ext;
		buf += ext;
	}

	/* NOTE: we can handle only one payload type */
	if (m_payload_type != payload_type)
	{
		P_LOG("payload err");
		return 0;
	}

	RtpPack rtpPack(seq, flags, timestamp, buf, len);

	if (m_first)
	{
		m_curRtpPacks.push_back(rtpPack);

		if (flags)
		{
			this->process_cur_rtp();
		}
		
		m_exp_seq = ++seq;
		m_first = false;
	} 
	else
	{
		if (m_exp_seq == seq)
		{
			m_curRtpPacks.push_back(rtpPack);
			++m_exp_seq;
			
			if (flags)
			{
				this->process_cur_rtp();
			}

			this->process_pend_rtp();		
		} 
		else
		{
			m_pendRtpPacks[seq] = rtpPack;

			if (m_pendRtpPacks.size() > 500)
			{
				P_LOG("pend rtp > 500");

				m_curRtpPacks.clear();
				m_exp_seq = m_pendRtpPacks.begin()->second.seq;

				this->process_pend_rtp();
			} 
		}
	}

	return 0;
}

void PSipClient::OnExit()
{
	PManager* pm = PManager::Instance();

	pm->AquireLock();
	pm->UnregistClient(m_url);
	pm->ReleaseLock();

	m_server->del_dialog(m_callid);

	PTaskMsg msg(EN_CLIENT_EXIT, NULL);

	for (auto it = m_addConn.begin(); it != m_addConn.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}

	for (auto it = m_pendPlay.begin(); it != m_pendPlay.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}

	PMediaClient::OnExit();
}

void PSipClient::GetMediaInfo(PRtspConn* conn)
{
	if (m_descbRsp.m_ret == 200)
	{
		conn->on_descb_rsp(m_descbRsp);
	}

	conn->AddRef();
	m_addConn.insert(conn);
}

int PSipClient::on_rtsp_req(PRtspReq& req, PRtspConn* conn)
{
	return -1;
}

void PSipClient::process_sip(osip_message_t* rsp, sip_dialog* dlg)
{
	if (rsp->contacts.nb_elt)
	{
		osip_contact_t* ct;
		osip_message_get_contact(rsp, 0, &ct);

		dlg->destUser = ct->url->username;
		dlg->destHost = ct->url->host;
		dlg->destPort = ct->url->port;
	}

	char* dest;
	osip_to_to_str(rsp->to, &dest);
	dlg->remoteTag = dest;
	osip_free(dest);

	if (rsp->content_length && rsp->content_type)
	{
		if (!strcasecmp(rsp->content_type->subtype, "sdp"))
		{
			sdp_message_t* sdp;
			osip_body_t* body;

			osip_message_get_body(rsp, 0, &body);
			
			sdp_message_init(&sdp);
			sdp_message_parse(sdp, body->body);

			m_payload_type = atoi(sdp_message_m_payload_get(sdp, 0, 0));

			sdp_message_free(sdp);
		}
	}

	osip_message_t* sip;
	int cseq = atoi(rsp->cseq->number);

	build_sip_msg(sip, dlg, cseq, "ACK");

	m_server->send_sip_rsp(sip, dlg->destHost.c_str(), dlg->destPort.c_str());

	P_LOG("invite ok");
}

int PSipClient::process_msg(PTaskMsg& msg)
{
	switch (msg.m_type)
	{
	case EN_CONN_EXIT:
	{
		PRtspConn* conn = (PRtspConn*)msg.m_body;

		m_pendPlay.erase(conn);
		m_addConn.erase(conn);

		conn->DelRef();

		if (m_addConn.empty() && m_pendPlay.empty())
		{
			P_LOG("sip conn empty");

			sip_dialog* dlg = m_server->get_dialog(m_callid);

			if (dlg)
			{
				osip_message_t* sip;

				build_sip_msg(sip, dlg, ++dlg->cseq, "BYE");		

				m_server->send_sip_rsp(sip, dlg->destHost.c_str(), dlg->destPort.c_str());
			}

			return -1;
		}
	}
	break;

	default:
		break;
	}

	return 0;
}

void PSipClient::process_pend_rtp()
{
	while (m_pendRtpPacks.size())
	{
		auto it = m_pendRtpPacks.find(m_exp_seq);
		if (it != m_pendRtpPacks.end())
		{
			RtpPack& rPack = it->second;

			m_curRtpPacks.push_back(rPack);
			if (rPack.flags)
			{
				this->process_cur_rtp();
			}

			++m_exp_seq;
			m_pendRtpPacks.erase(it);
		}
		else
		{
			break;
		}
	}
}

static int avio_r8(uint8_t*& pb)
{
	int val = *pb;
	++pb;
	return val;
}

static unsigned int avio_rb16(uint8_t*& s)
{
	unsigned int val;
	val = avio_r8(s) << 8;
	val |= avio_r8(s);
	return val;
}

static void avio_skip(uint8_t*& pb, int64_t offset)
{
	pb += offset;
}

static unsigned int avio_rb32(uint8_t*& s)
{
	unsigned int val;
	val = avio_rb16(s) << 16;
	val |= avio_rb16(s);
	return val;
}

static long mpegps_psm_parse(uint8_t*& pb)
{
	int psm_length, ps_info_length, es_map_length;

	psm_length = avio_rb16(pb);
	avio_r8(pb);
	avio_r8(pb);
	ps_info_length = avio_rb16(pb);

	/* skip program_stream_info */
	avio_skip(pb, ps_info_length);
	/*es_map_length = */avio_rb16(pb);
	/* Ignore es_map_length, trust psm_length */
	es_map_length = psm_length - ps_info_length - 10;

	/* at least one es available? */
	while (es_map_length >= 4) {
		unsigned char type = avio_r8(pb);
		unsigned char es_id = avio_r8(pb);
		uint16_t es_info_length = avio_rb16(pb);

		/* remember mapping from stream id to stream type */
		//m->psm_es_type[es_id] = type;
		P_LOG("es id:%x type:%x", es_id, type);
		/* skip program_stream_info */
		avio_skip(pb, es_info_length);
		es_map_length -= 4 + es_info_length;
	}
	avio_rb32(pb); /* crc32 */
	return 2 + psm_length;
}

static int find_next_start_code(uint8_t*& pb, int *size_ptr,
	int32_t *header_state)
{
	unsigned int state, v;
	int val, n;

	state = *header_state;
	n = *size_ptr;
	while (n > 0) {
		v = avio_r8(pb);
		n--;
		if (state == 0x000001) {
			state = ((state << 8) | v) & 0xffffff;
			val = state;
			goto found;
		}
		state = ((state << 8) | v) & 0xffffff;
	}
	val = -1;

found:
	*header_state = state;
	*size_ptr = n;
	return val;
}

static inline int64_t ff_parse_pes_pts(const uint8_t *buf) {
	return (int64_t)(*buf & 0x0e) << 29 |
		(AV_RB16(buf + 1) >> 1) << 15 |
		AV_RB16(buf + 3) >> 1;
}

static int64_t get_pts(uint8_t*& pb, int c)
{
	uint8_t buf[5];

	buf[0] = c < 0 ? avio_r8(pb) : c;
	memcpy(buf + 1, pb, 4);
	pb += 4;

	return ff_parse_pes_pts(buf);
}


/* read the next PES header. Return its position in ppos
* (if not NULL), and its start code, pts and dts.
*/
static int mpegps_read_pes_header(uint8_t*& pb, uint8_t* pb_start, const int in_size, int *pstart_code,
	int64_t *ppts, int64_t *pdts)
{
	int len, size, startcode, c, flags, header_len;
	int pes_ext, ext2_len, id_ext, skip;
	int64_t pts, dts;
	int header_state;

redo:
	/* next start code (should be immediately after) */
	header_state = 0xff;
	size = in_size - (pb - pb_start);
	startcode = find_next_start_code(pb, &size, &header_state);
	
	P_LOG("startcode:%x, %d", startcode, startcode);
	
	if (startcode < 0) {
		return -1;
	}

	if (startcode == PACK_START_CODE)
		goto redo;
	if (startcode == SYSTEM_HEADER_START_CODE)
		goto redo;
	if (startcode == PADDING_STREAM) {
		int off = avio_rb16(pb);
		avio_skip(pb, off);
		goto redo;
	}

	if (startcode == PROGRAM_STREAM_MAP) {
		mpegps_psm_parse(pb);
		goto redo;
	}

	/* find matching stream */
	if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
		(startcode >= 0x1e0 && startcode <= 0x1ef) ||
		(startcode == 0x1bd) ||
		(startcode == PRIVATE_STREAM_2) ||
		(startcode == 0x1fd)))
		goto redo;
		
	len = avio_rb16(pb);
	pts = dts = AV_NOPTS_VALUE;

	if (startcode != PRIVATE_STREAM_2)
	{
		/* stuffing */
		for (;;) {
			if (len < 1)
			{
				return -1;
			}
			c = avio_r8(pb);
			len--;
			/* XXX: for MPEG-1, should test only bit 7 */
			if (c != 0xff)
				break;
		}
		if ((c & 0xc0) == 0x40) {
			/* buffer scale & size */
			avio_r8(pb);
			c = avio_r8(pb);
			len -= 2;
		}
		if ((c & 0xe0) == 0x20) {
			dts =
				pts = get_pts(pb, c);
			len -= 4;
			if (c & 0x10) {
				dts = get_pts(pb, -1);
				len -= 5;
			}
		}
		else if ((c & 0xc0) == 0x80) {
			/* mpeg 2 PES */
			flags = avio_r8(pb);
			header_len = avio_r8(pb);
			len -= 2;
			if (header_len > len)
			{
				return -1;
			}
			len -= header_len;
			if (flags & 0x80) {
				dts = pts = get_pts(pb, -1);
				header_len -= 5;
				if (flags & 0x40) {
					dts = get_pts(pb, -1);
					header_len -= 5;
				}
			}
			if (flags & 0x3f && header_len == 0) {
				flags &= 0xC0;
				P_LOG("Further flags set but no bytes left\n");
			}
			if (flags & 0x01) { /* PES extension */
				pes_ext = avio_r8(pb);
				header_len--;
				/* Skip PES private data, program packet sequence counter
				* and P-STD buffer */
				skip = (pes_ext >> 4) & 0xb;
				skip += skip & 0x9;
				if (pes_ext & 0x40 || skip > header_len) {
					P_LOG("pes_ext %X is invalid\n", pes_ext);
					pes_ext = skip = 0;
				}
				avio_skip(pb, skip);
				header_len -= skip;

				if (pes_ext & 0x01) { /* PES extension 2 */
					ext2_len = avio_r8(pb);
					header_len--;
					if ((ext2_len & 0x7f) > 0) {
						id_ext = avio_r8(pb);
						if ((id_ext & 0x80) == 0)
							startcode = ((startcode & 0xff) << 8) | id_ext;
						header_len--;
					}
				}
			}
			if (header_len < 0)
			{
				return -1;
			}
			avio_skip(pb, header_len);
		}
		else if (c != 0xf)
			goto redo;
	}

	if (len < 0)
	{
		return -1;
	}

	*pstart_code = startcode;
	*ppts = pts;
	*pdts = dts;

	return len;
}


void PSipClient::process_cur_rtp()
{
	uint8_t* buf = m_packBuf;
	int lenx = 0;

	for (auto it = m_curRtpPacks.begin(); it != m_curRtpPacks.end(); ++it)
	{
		memcpy(buf, it->buf, it->len);
		buf += it->len;
		lenx += it->len;
	}

	m_curRtpPacks.clear();

	int len, startcode, i, es_type, ret;
	int lpcm_header_len = -1; //Init to suppress warning
	int request_probe = 0;
	int64_t pts, dts, dummy_pos; // dummy_pos is needed for the index building to work
	uint8_t* pb = m_packBuf;
	

redo:
	dummy_pos = lenx - (pb - m_packBuf);
	len = mpegps_read_pes_header(pb, m_packBuf, dummy_pos, &startcode, &pts, &dts);
	if (len < 0)
		return ;

	P_LOG("startcode:%x", startcode);

	if (startcode >= 0x80 && startcode <= 0xcf) {
		if (len < 4)
			goto skip;

		//if (!m->raw_ac3) {
		/* audio: skip header */
		avio_r8(pb);
		lpcm_header_len = avio_rb16(pb);
		len -= 3;
		if (startcode >= 0xb0 && startcode <= 0xbf) {
			/* MLP/TrueHD audio has a 4-byte header */
			avio_r8(pb);
			len--;
		}
		//}
	}

	skip:
		/* skip packet */
		avio_skip(pb, len);
		goto redo;


found:
	//
		return;

}

PSipClient::RtpPack::RtpPack(uint16_t iseq, int iflags, uint16_t its, uint8_t* ibuf, int ilen)
	: seq(seq)
	, flags(iflags)
	, timestamp(its)
	, len(ilen)
{
	memcpy(buf, ibuf, ilen);
}

PSipClient::RtpPack::RtpPack(const RtpPack& rPack)
	: seq(rPack.seq)
	, flags(rPack.flags)
	, timestamp(rPack.timestamp)
	, len(rPack.len)
{
	memcpy(buf, rPack.buf, rPack.len);
}

PSipClient::RtpPack::RtpPack()
{

}
