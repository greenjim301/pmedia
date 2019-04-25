#include "prtmp_client.h"
#include <stdlib.h>
#include "prtsp_comm.h"
#include "pmanager.h"
#include "plog.h"
#include "prtsp_conn.h"

PRtmpClient::PRtmpClient(PString& ulr)
	: m_url(ulr)
	, m_bfirst(true)
	, m_videoseq(0)
	, m_nAudio(0)
{

}

int PRtmpClient::on_rtsp_req(PRtspReq& req, PRtspConn* conn)
{
	switch (req.m_method)
	{
	case rtsp_method::SETUP:
	{
		PRtspRsp rsp;

		rsp.m_ret = 200;
		rsp.m_descb = "OK";
		rsp.m_session = "42315";
		rsp.m_session.append(";timeout=60", strlen(";timeout=60"));

		char* p = strstr(req.m_url.data(), "trackID=0");

		if (p)
		{
			rsp.m_transport = "RTP/AVP/TCP;unicast;interleaved=0-1";
		}
		else
		{
			rsp.m_transport = "RTP/AVP/TCP;unicast;interleaved=2-3";
		}

		conn->on_rtsp_rsp(rsp);
	}
	break;

	case rtsp_method::PLAY:
	{
		m_addConn.erase(conn);
		m_pendPlay.insert(conn);

		PRtspRsp rsp;

		rsp.m_ret = 200;
		rsp.m_descb = "OK";
		rsp.m_session = req.m_session;
		rsp.m_range = req.m_range;

		conn->on_rtsp_rsp(rsp);
	}
	break;

	default:
		break;
	}

	return 0;
}

void PRtmpClient::OnRun()
{
	RTMP_Init(&m_rtmp);
	char* buf = (char*)malloc(1024 * 1024);
	int size = 1024 * 1024;
	int ret, nread;
	struct epoll_event events[DEF_MAX_EVENTS];
	std::vector<PTaskMsg> msgs;
	int offset = 0;

	if (!RTMP_SetupURL(&m_rtmp, m_url.data()))
	{
		goto end;
	}

	if (!RTMP_Connect(&m_rtmp, NULL))
	{
		goto end;
	}

	if (!RTMP_ConnectStream(&m_rtmp, 0))
	{
		goto end;
	}

	this->AddInEvent(m_rtmp.m_sb.sb_socket);

	for (;;)
	{
		ret = this->WaitEvent(events, msgs, -1);
		if (ret < 0)
		{
			P_LOG("epoll wait err:%d", errno);
			goto end;
		}
		else
		{
			for (int i = 0; i < ret; ++i)
			{
				struct epoll_event& ev = events[i];

				if (ev.data.fd == m_rtmp.m_sb.sb_socket)
				{
					nread = RTMP_Read(&m_rtmp, buf + offset, size - offset);

					if (nread <= 0)
					{
						P_LOG("rtmp read ret:%d", nread);
						goto end;
					}
					else
					{
						this->parse_flv(buf, offset + nread, offset);
					}
				}
				else
				{
					P_LOG("ev fd:%d in sock:%d", ev.data.fd, m_rtmp.m_sb.sb_socket);
					goto end;
				}
			}

			for (auto it = msgs.begin(); it != msgs.end(); ++it)
			{
				if (this->process_msg(*it))
				{
					goto end;
				}
			}
		}
	}

end:
	free(buf);
	RTMP_Close(&m_rtmp);
}

enum FlvTagType {
	FLV_TAG_TYPE_AUDIO = 0x08,
	FLV_TAG_TYPE_VIDEO = 0x09,
	FLV_TAG_TYPE_META = 0x12,
};

enum {
	FLV_STREAM_TYPE_VIDEO,
	FLV_STREAM_TYPE_AUDIO,
	FLV_STREAM_TYPE_SUBTITLE,
	FLV_STREAM_TYPE_DATA,
	FLV_STREAM_TYPE_NB,
};

enum {
	FLV_MONO = 0,
	FLV_STEREO = 1,
};

/* bitmasks to isolate specific values */
#define FLV_AUDIO_CHANNEL_MASK    0x01
#define FLV_AUDIO_SAMPLESIZE_MASK 0x02
#define FLV_AUDIO_SAMPLERATE_MASK 0x0c
#define FLV_AUDIO_CODECID_MASK    0xf0

/* offsets for packed values */
#define FLV_AUDIO_SAMPLESSIZE_OFFSET 1
#define FLV_AUDIO_SAMPLERATE_OFFSET  2
#define FLV_AUDIO_CODECID_OFFSET     4

#define FLV_VIDEO_CODECID_MASK    0x0f
#define FLV_VIDEO_FRAMETYPE_MASK  0xf0

enum {
	FLV_CODECID_H263 = 2,
	FLV_CODECID_SCREEN = 3,
	FLV_CODECID_VP6 = 4,
	FLV_CODECID_VP6A = 5,
	FLV_CODECID_SCREEN2 = 6,
	FLV_CODECID_H264 = 7,
	FLV_CODECID_REALH263 = 8,
	FLV_CODECID_MPEG4 = 9,
};

#define FLV_VIDEO_FRAMETYPE_OFFSET   4

enum {
	FLV_FRAME_KEY = 1 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< key frame (for AVC, a seekable frame)
	FLV_FRAME_INTER = 2 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< inter frame (for AVC, a non-seekable frame)
	FLV_FRAME_DISP_INTER = 3 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< disposable inter frame (H.263 only)
	FLV_FRAME_GENERATED_KEY = 4 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< generated key frame (reserved for server use only)
	FLV_FRAME_VIDEO_INFO_CMD = 5 << FLV_VIDEO_FRAMETYPE_OFFSET, ///< video info/command frame
};

static const uint8_t start_sequence[] = { 0, 0, 0, 1 };

void PRtmpClient::parse_flv(uint8_t* inbuf, int insize, int& inoffset)
{
	uint8_t* pb = inbuf;
	int pre_tag_size, type,
		size, next, flags;
	int64_t dts;
	int offset = 0;

	if (m_bfirst)
	{
		if (insize < 13)
		{
			printf("first insize:%d < 13\n", insize);
			return;
		}

		avio_skip(pb, 4);
		flags = avio_r8(pb);
		offset = avio_rb32(pb);
		m_bfirst = false;

		if (insize < offset)
		{
			printf("insize:%d < offset:%d\n", insize, offset);
			return;
		}

		pre_tag_size = avio_rb32(pb);
		offset += 4;
	}

	pb = inbuf + offset;
	int leftsize = insize - offset;

	while (leftsize > 11)
	{
		//pre_tag_size = avio_rb32(pb);

		type = (avio_r8(pb) & 0x1F);
		size = avio_rb24(pb);
		size += 4;

		dts = avio_rb24(pb);
		dts |= (unsigned)avio_r8(pb) << 24;

		avio_skip(pb, 3); /* stream id, always 0 */

		if (leftsize - 11 < size)
		{
			printf("left size:%d parse size:%d\n", leftsize - 11, size);			
			break;
		}

		next = size + (pb - inbuf);

		if (type == FLV_TAG_TYPE_AUDIO) {
			flags = avio_r8(pb);
			size--;

			int bits_per_coded_sample = (flags & FLV_AUDIO_SAMPLESIZE_MASK) ? 16 : 8;
			int flv_codecid = flags & FLV_AUDIO_CODECID_MASK;
			int channels = (flags & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;
			int sample_rate = 44100 << ((flags & FLV_AUDIO_SAMPLERATE_MASK) >>
				FLV_AUDIO_SAMPLERATE_OFFSET) >> 3;

			printf("bits:%d codec:%d channel:%d sample:%d\n", bits_per_coded_sample, flv_codecid, channels, sample_rate);
		}
		else if (type == FLV_TAG_TYPE_VIDEO) {
			flags = avio_r8(pb);
			size--;

			int codecid = flags & FLV_VIDEO_CODECID_MASK;
			int frame_type = flags & FLV_VIDEO_FRAMETYPE_MASK;

			if (frame_type > FLV_FRAME_INTER || codecid != FLV_CODECID_H264)
			{
				goto skip;
			}

			this->on_sdp_info(m_nAudio);

			int packet_type = avio_r8(pb);
			size--;

			int32_t cts = (avio_rb24(pb) + 0xff800000) ^ 0xff800000;
			int64_t pts = dts + cts;
			size -= 3;

			//printf("frame type:%d packet type:%d size:%d, dts:%lld\n", frame_type, packet_type, size - 4, dts);

			if (packet_type == 0)
			{
				avio_r8(pb);
				int avProfile = avio_r8(pb);
				avio_skip(pb, 3);

				int numSps = (avio_r8(pb) & 0x1f);
				char tempbuf[256];
				dts *= 90;

				for (int i = 0; i < numSps; ++i)
				{
					int spsLen = avio_rb16(pb);

					memcpy(tempbuf, start_sequence, sizeof(start_sequence));
					memcpy(&tempbuf[4], pb, spsLen);
					parse_send_es(tempbuf, spsLen + 4, dts, 96, m_sendbuf, m_videoseq, m_pendPlay);

					avio_skip(pb, spsLen);
					printf("spsLen:%d\n", spsLen);					
				}

				int numPps = avio_r8(pb);
				for (int i = 0; i < numPps; ++i)
				{
					int ppsLen = avio_rb16(pb);
					avio_skip(pb, ppsLen);

					memcpy(tempbuf, start_sequence, sizeof(start_sequence));
					memcpy(&tempbuf[4], pb, ppsLen);
					parse_send_es(tempbuf, ppsLen + 4, dts, 96, m_sendbuf, m_videoseq, m_pendPlay);

					printf("ppsLen:%d\n", ppsLen);
				}

				printf("profile:%d\n", avProfile);
			}
			else
			{
				size -= 4;
				int tot = size;
				uint8_t* fp = pb;
				int fsize;

				while (size > 0)
				{
					fsize = avio_rb32(fp);
					memcpy(fp - 4, start_sequence, sizeof(start_sequence));
					avio_skip(fp, fsize);

					size -= 4 + fsize;
				}

				dts *= 90;
				parse_send_es(pb, tot, dts, 96, m_sendbuf, m_videoseq, m_pendPlay);
			}
		
		}
		else if (type == FLV_TAG_TYPE_META) {
			goto skip;
		}

	skip:
		pb = inbuf + next;
		leftsize = insize - next;
	}

	inoffset = leftsize;

	if (leftsize != 0)
	{
		printf("xxx %d\n", leftsize);
		memmove(inbuf, inbuf + insize - leftsize, leftsize);
	}
}

void PRtmpClient::OnExit()
{
	P_LOG("rtmp client exit");

	PManager* pm = PManager::Instance();

	pm->AquireLock();
	pm->UnregistClient(m_url);
	pm->ReleaseLock();

	PMediaClient::OnExit();
}

int PRtmpClient::process_msg(PTaskMsg& msg)
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
			P_LOG("rtmp conn empty");

			return -1;
		}
	}
	break;

	default:
		break;
	}

	return 0;
}

void PRtmpClient::on_sdp_info(int nAudio)
{
	if (m_descbRsp.m_ret == 200)
	{
		return;
	}

	m_descbRsp.m_ret = 200;
	m_descbRsp.m_descb = "OK";
	m_descbRsp.m_cntType = "application/sdp";
	m_descbRsp.m_cntBase = m_url;

	PString tmp = "v=0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "o=- 2252478537 2252478537 IN IP4 0.0.0.0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "s=RTSP Session/2.0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "c=IN IP4 0.0.0.0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "t=0 0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "a=control:*";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "a=range:npt=now-";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "m=video 0 RTP/AVP 96";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "a=control:trackID=0";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "a=rtpmap:96 H264/90000";
	m_descbRsp.m_sdp.push_back(tmp);

	tmp = "a=recvonly";
	m_descbRsp.m_sdp.push_back(tmp);

	for (auto it = m_addConn.begin(); it != m_addConn.end(); ++it)
	{
		(*it)->on_descb_rsp(m_descbRsp);
	}
}
