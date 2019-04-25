#include "prtsp_comm.h"
#include <cstdlib>
#include "prtsp_conn.h"

void skip_space(char*& p)
{
	while (*p != '\0' && *p == ' ')
	{
		++p;
	}
}


void build_sip_msg(osip_message_t*& sip, sip_dialog* dlg, int cseq, const char* method)
{
	osip_message_init(&sip);

	osip_uri_t* uri;
	osip_uri_init(&uri);

	osip_uri_set_scheme(uri, osip_strdup("sip"));
	osip_uri_set_username(uri, osip_strdup(dlg->destUser.c_str()));
	osip_uri_set_host(uri, osip_strdup(dlg->destHost.c_str()));
	osip_uri_set_port(uri, osip_strdup(dlg->destPort.c_str()));

	osip_message_set_method(sip, osip_strdup(method));
	osip_message_set_uri(sip, uri);
	osip_message_set_version(sip, osip_strdup("SIP/2.0"));

	char temp[128];

	sprintf(temp, "%s;branch=z9hG4bK%d", dlg->via.c_str(), std::rand());
	osip_message_set_via(sip, temp);

	osip_message_set_from(sip, dlg->localTag.c_str());
	osip_message_set_to(sip, dlg->remoteTag.c_str());
	osip_message_set_call_id(sip, dlg->callid.c_str());

	sprintf(temp, "%d %s", cseq, method);	
	osip_message_set_cseq(sip, temp);

	osip_message_set_contact(sip, dlg->localContact.c_str());
	osip_message_set_max_forwards(sip, "70");
}

int avio_r8(uint8_t*& pb)
{
	int val = *pb;
	++pb;
	return val;
}

unsigned int avio_rb16(uint8_t*& s)
{
	unsigned int val;
	val = avio_r8(s) << 8;
	val |= avio_r8(s);
	return val;
}

void avio_skip(uint8_t*& pb, int64_t offset)
{
	pb += offset;
}

unsigned int avio_rb32(uint8_t*& s)
{
	unsigned int val;
	val = avio_rb16(s) << 16;
	val |= avio_rb16(s);
	return val;
}

unsigned int avio_rb24(uint8_t*& s)
{
	unsigned int val;
	val = avio_rb16(s) << 8;
	val |= avio_r8(s);
	return val;
}

const uint8_t * ff_avc_find_startcode_internal(
	const uint8_t *p, const uint8_t *end)
{
	const uint8_t *a = p + 4 - ((intptr_t)p & 3);

	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t*)p;
		//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
		//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
		if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p + 1;
			}
			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p + 2;
				if (p[4] == 0 && p[5] == 1)
					return p + 3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}

const uint8_t * ff_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
	const uint8_t *out = ff_avc_find_startcode_internal(p, end);
	if (p < out && out < end && !out[-1]) out--;
	return out;
}

void parse_send_es(const uint8_t* inbuf, const int insize, int64_t ts,
	int payload, uint8_t* sendbuf, int& seq, std::set<PRtspConn *>& pendPlay)
{
	const uint8_t *r, *end = inbuf + insize;

	r = ff_avc_find_startcode(inbuf, end);

	while (r < end) {
		const uint8_t *r1;

		while (!*(r++));
		r1 = ff_avc_find_startcode(r, end);

		nal_send(r, r1 - r, r1 == end, ts, payload, sendbuf, seq, pendPlay);
		r = r1;
	}
}

void nal_send(const uint8_t *buf, int size, int last, uint32_t ts, 
	int payload, uint8_t* sendbuf, int& seq, std::set<PRtspConn *>& pendPlay)
{
	if (size <= MAX_PAYLOAD_SIZE)
	{
		uint8_t* p_rtp = sendbuf;

		avio_w8(p_rtp, '$');
		avio_w8(p_rtp, 0);
		avio_wb16(p_rtp, size + 12);

		avio_w8(p_rtp, RTP_VERSION << 6);
		avio_w8(p_rtp, (payload & 0x7f) | ((last & 0x01) << 7));
		avio_wb16(p_rtp, seq);
		avio_wb32(p_rtp, ts);
		avio_wb32(p_rtp, 0);

		seq = (seq + 1) & 0xffff;

		memcpy(sendbuf + 4 + 12, buf, size);

		for (auto it = pendPlay.begin(); it != pendPlay.end(); ++it)
		{
			(*it)->send_tcp_stream(sendbuf, size + 4 + 12);
		}
	}
	else {
		uint8_t* p_buf = sendbuf;

		avio_w8(p_buf, '$');
		avio_w8(p_buf, 0);
		avio_wb16(p_buf, MAX_PAYLOAD_SIZE + 12);

		avio_w8(p_buf, RTP_VERSION << 6);
		avio_w8(p_buf, payload & 0x7f);
		avio_wb16(p_buf, seq);
		avio_wb32(p_buf, ts);
		avio_wb32(p_buf, 0);

		int flag_byte, header_size;

		uint8_t type = buf[0] & 0x1F;
		uint8_t nri = buf[0] & 0x60;

		p_buf[0] = 28;        /* FU Indicator; Type = 28 ---> FU-A */
		p_buf[0] |= nri;
		p_buf[1] = type;
		p_buf[1] |= 1 << 7;
		buf += 1;
		size -= 1;

		flag_byte = 1;
		header_size = 2;
		uint8_t* ptmp;

		while (size + header_size > MAX_PAYLOAD_SIZE) {
			memcpy(&p_buf[header_size], buf, MAX_PAYLOAD_SIZE - header_size);

			ptmp = sendbuf + 6;
			avio_wb16(ptmp, seq);

			seq = (seq + 1) & 0xffff;

			for (auto it = pendPlay.begin(); it != pendPlay.end(); ++it)
			{
				(*it)->send_tcp_stream(sendbuf, MAX_PAYLOAD_SIZE + 4 + 12);
			}

			buf += MAX_PAYLOAD_SIZE - header_size;
			size -= MAX_PAYLOAD_SIZE - header_size;
			p_buf[flag_byte] &= ~(1 << 7);
		}

		p_buf[flag_byte] |= 1 << 6;
		memcpy(&p_buf[header_size], buf, size);

		ptmp = (uint8_t*)sendbuf + 2;
		avio_wb16(ptmp, size + header_size + 12);

		ptmp = (uint8_t*)sendbuf + 5;
		avio_w8(ptmp, (payload & 0x7f) | ((last & 0x01) << 7));

		ptmp = (uint8_t*)sendbuf + 6;
		avio_wb16(ptmp, seq);

		seq = (seq + 1) & 0xffff;

		for (auto it = pendPlay.begin(); it != pendPlay.end(); ++it)
		{
			(*it)->send_tcp_stream(sendbuf, size + header_size + 4 + 12);
		}
	}
}

void avio_w8(uint8_t*& s, int b)
{
	*s++ = b;
}

void avio_wb16(uint8_t*& s, unsigned int val)
{
	avio_w8(s, (int)val >> 8);
	avio_w8(s, (uint8_t)val);
}

void avio_wb32(uint8_t*& s, unsigned int val)
{
	avio_w8(s, val >> 24);
	avio_w8(s, (uint8_t)(val >> 16));
	avio_w8(s, (uint8_t)(val >> 8));
	avio_w8(s, (uint8_t)val);
}