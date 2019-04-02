#include "psip_conn.h"
#include "psip_server.h"
#include "osipparser2/osip_md5.h"
#include "plog.h"
#include "tinyxml2/tinyxml2.h"
#include "pmanager.h"
#include "psip_client.h"

using namespace tinyxml2;

static void cvt_to_hex(unsigned char* in, unsigned char* out)
{
	unsigned short i;
	unsigned char j;
	for (i = 0; i < 16; i++)
	{
		j = (in[i] >> 4) & 0xf;
		if (j <= 9) out[i * 2] = (j + '0');
		else out[i * 2] = (j + 'a' - 10);
		j = in[i] & 0xf;
		if (j <= 9) out[i * 2 + 1] = (j + '0');
		else out[i * 2 + 1] = (j + 'a' - 10);
	}
}

PSipConn::PSipConn(PSipServer* server)
	: m_server(server)
	, m_contact(NULL)
	, m_cseq(0)
{
}

PSipConn::~PSipConn()
{
	if (m_contact)
	{
		osip_contact_free(m_contact);
	}
}

void PSipConn::clone_basic(osip_message_t* sip, osip_message_t* rsp, int sCode)
{
	osip_message_set_status_code(rsp, sCode);
	osip_message_set_reason_phrase(rsp, osip_strdup(osip_message_get_reason(sCode)));

	osip_from_clone(sip->from, &rsp->from);
	osip_to_clone(sip->to, &rsp->to);
	osip_call_id_clone(sip->call_id, &rsp->call_id);
	osip_cseq_clone(sip->cseq, &rsp->cseq);
	osip_list_clone(&sip->vias, &rsp->vias, (int(*)(void *, void **)) &osip_via_clone);

	char temp[64];
	sprintf(temp, "%d", std::rand());
	osip_to_set_tag(rsp->to, osip_strdup(temp));
}

int PSipConn::process_req(osip_message_t* sip, sockaddr_in& in_addr, socklen_t in_addrlen)
{
	osip_message_t* rsp;
	osip_message_init(&rsp);

	if (MSG_IS_REGISTER(sip))
	{
		if (sip->authorizations.nb_elt == 0)
		{
			char temp[128];
			this->clone_basic(sip, rsp, 401);		

			sprintf(temp, "%016lx", std::rand());
			m_nonce = temp;

			sprintf(temp, "Digest realm=\"%s\",nonce=\"%s\",algorithm=MD5",
				m_server->get_domain().c_str(), m_nonce.c_str());
			osip_message_set_www_authenticate(rsp, temp);

			m_server->send_sip_rsp(rsp, in_addr, in_addrlen);
		}
		else
		{
			osip_authorization_t* auth;
			
			osip_message_get_authorization(sip, 0, &auth);

			char* nonce = osip_authorization_get_nonce(auth);

			if (0 != memcmp(nonce + 1, m_nonce.c_str(), m_nonce.size()))
			{
				this->clone_basic(sip, rsp, 403);
				m_server->send_sip_rsp(rsp, in_addr, in_addrlen);

				return -1;
			}

			char* methond = osip_message_get_method(sip);
			char* username = osip_authorization_get_username(auth);
			char* realm = osip_authorization_get_realm(auth);
			char* uri = osip_authorization_get_uri(auth);
			char* response = osip_authorization_get_response(auth);

			unsigned char ha1[16];
			unsigned char ha2[16];
			unsigned char res[16];

			unsigned char ha1_hex[32];
			unsigned char ha2_hex[32];
			unsigned char res_hex[32];

			unsigned char c1 = ':';
			PString pass = m_server->get_passwd();

			osip_MD5_CTX md5_ctx;

			osip_MD5Init(&md5_ctx);
			osip_MD5Update(&md5_ctx, (unsigned char*)username + 1, strlen(username) - 2);
			osip_MD5Update(&md5_ctx, &c1, 1);
			osip_MD5Update(&md5_ctx, (unsigned char*)realm + 1, strlen(realm) - 2);
			osip_MD5Update(&md5_ctx, &c1, 1);
			osip_MD5Update(&md5_ctx, (unsigned char*)pass.data(), pass.size());
			osip_MD5Final(ha1, &md5_ctx);
			cvt_to_hex(ha1, ha1_hex);

			osip_MD5Init(&md5_ctx);
			osip_MD5Update(&md5_ctx, (unsigned char*)methond, strlen(methond));
			osip_MD5Update(&md5_ctx, &c1, 1);
			osip_MD5Update(&md5_ctx, (unsigned char*)uri + 1, strlen(uri) - 2);
			osip_MD5Final(ha2, &md5_ctx);
			cvt_to_hex(ha2, ha2_hex);

			osip_MD5Init(&md5_ctx);
			osip_MD5Update(&md5_ctx, ha1_hex, 32);
			osip_MD5Update(&md5_ctx, &c1, 1);
			osip_MD5Update(&md5_ctx, (unsigned char*)nonce + 1, strlen(nonce) - 2);
			osip_MD5Update(&md5_ctx, &c1, 1);
			osip_MD5Update(&md5_ctx, ha2_hex, 32);
			osip_MD5Final(res, &md5_ctx);
			cvt_to_hex(res, res_hex);

			if (0 != memcmp(response + 1, res_hex, 32))
			{
				this->clone_basic(sip, rsp, 403);
				m_server->send_sip_rsp(rsp, in_addr, in_addrlen);

				return -1;
			}
			else
			{
				if (m_contact)
				{
					osip_contact_free(m_contact);
				}

				if (sip->contacts.nb_elt)
				{
					osip_contact_t* contact;
					osip_message_get_contact(sip, 0, &contact);
					
					osip_contact_clone(contact, &m_contact);

					osip_list_clone(&sip->contacts, &rsp->contacts, (int(*)(void *, void **)) &osip_contact_clone);
				}
				else 
				{
					osip_contact_clone(sip->to, &m_contact);

					char* dest;

					osip_to_to_str(sip->to, &dest);
					osip_message_set_contact(rsp, dest);

					osip_free(dest);
				}

				osip_header_t* expire;

				osip_message_get_expires(sip, 0, &expire);
				if (expire)
				{
					osip_message_set_expires(rsp, expire->hvalue);
				}
				else
				{
					osip_message_set_expires(rsp, "7200");
				}

				this->clone_basic(sip, rsp, 200);
				
				char temp[64];
				time_t tt;
				struct tm atm;

				time(&tt);
				localtime_r(&tt, &atm);

				sprintf(temp, "%04d-%02d-%02dT%02d:%02d:%02d",
					atm.tm_year + 1900, atm.tm_mon + 1, atm.tm_mday, atm.tm_hour, atm.tm_min, atm.tm_sec);

				osip_message_set_date(rsp, temp);

				m_server->send_sip_rsp(rsp, in_addr, in_addrlen);

				P_LOG("register ok");

				return 0;
			}
		}
	}
	else if (MSG_IS_MESSAGE(sip) && m_contact)
	{
		osip_body_t* body;
		osip_message_get_body(sip, 0, &body);

		if (body)
		{
			XMLDocument doc;
			XMLError ret = doc.Parse(body->body, body->length);
			
			if (ret)
			{
				P_LOG("xml parse err:%d", ret);
			}
			else
			{
				XMLText* textNode = doc.FirstChildElement("Notify")->FirstChildElement("CmdType")->FirstChild()->ToText();
				//P_LOG("cmd: %s", textNode->Value());
			}
		}

		this->clone_basic(sip, rsp, 200);
		m_server->send_sip_rsp(rsp, in_addr, in_addrlen);
	}
	else
	{
		this->clone_basic(sip, rsp, 403);
		m_server->send_sip_rsp(rsp, in_addr, in_addrlen);

		return -1;
	}

	return 0;
}

PMediaClient* PSipConn::init_invite(PString& channel, PString& url)
{
	if (!m_contact)
	{
		P_LOG("no contact");
		return NULL;
	}

	sip_dialog* sipDialog = new sip_dialog;

	sipDialog->destUser = channel;
	sipDialog->destHost = m_contact->url->host;
	sipDialog->destPort = m_contact->url->port;

	char temp[128];
	const PString& sDomian = m_server->get_domain();

	sprintf(temp, "SIP/2.0/UDP %s:%d", m_server->get_ip().c_str(), m_server->get_port());
	sipDialog->via = temp;

	sprintf(temp, "<sip:%s@%s:%d>;tag=%d", sDomian.c_str(), m_server->get_ip().c_str(), m_server->get_port(), std::rand());
	sipDialog->localTag = temp;

	sprintf(temp, "<sip:%s@%s:%s>", channel.c_str(), m_contact->url->host, m_contact->url->port);
	sipDialog->remoteTag = temp;

	sprintf(temp, "%d", std::rand());
	sipDialog->callid = temp;

	sipDialog->cseq = ++m_cseq;

	sprintf(temp, "<sip:%s@%s:%d>", sDomian.c_str(), m_server->get_ip().c_str(), m_server->get_port());
	sipDialog->localContact = temp;

	osip_message_t* sip;

	build_sip_msg(sip, sipDialog, sipDialog->cseq, "INVITE");

	osip_message_set_content_type(sip, "Application/SDP");

	sprintf(temp, "%s:0,%s:0", channel.c_str(), sDomian.c_str());
	osip_message_set_subject(sip, temp);

	PManager* pm = PManager::Instance();
	int rtp_sock;
	uint16_t rtp_port;
	PString rtp_ip;

	pm->CreateUdpSock(rtp_sock, rtp_ip, rtp_port);

	char sdp[1024];
	char* p = sdp;

	int n = sprintf(p, "v=0\r\n");
	p += n;

	n = sprintf(p, "o=%s 0 0 IN IP4 %s\r\n", channel.c_str(), rtp_ip.c_str());
	p += n;

	n = sprintf(p, "s=Play\r\n");
	p += n;

	n = sprintf(p, "c=IN IP4 %s\r\n", rtp_ip.c_str());
	p += n;

	n = sprintf(p, "t=0 0\r\n");
	p += n;

	n = sprintf(p, "m=video %d RTP/AVP 96\r\n", rtp_port);
	p += n;

	n = sprintf(p, "a=rtpmap:96 PS/90000\r\n");
	p += n;

	n = sprintf(p, "a=recvonly\r\n");
	p += n;

	n = sprintf(p, "y=0999999999\r\n");
	p += n;

	int length = p - sdp;
	sprintf(temp, "%d", length);

	osip_message_set_content_length(sip, temp);	
	osip_message_set_body(sip, sdp, length);

	m_server->send_sip_rsp(sip, m_contact->url->host, m_contact->url->port);

	PSipClient* sipClient = new PSipClient(rtp_sock, sipDialog->callid, url, m_server);
	
	sipClient->Start();
	sipDialog->sipClient = sipClient;
	m_server->add_dialog(sipDialog);
	
	return sipClient;
}
