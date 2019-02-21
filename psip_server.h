#pragma once

#include "ptask.h"
#include <map>
#include "osipparser2/osip_parser.h"
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pstring.h"
#include "prtsp_comm.h"

class PSipConn;
class PSipClient;
class PMediaClient;

class PSipServer : public PTask
{
public:
	PSipServer(PString& domain, PString& pwd, PString& ip, uint16_t port);

	void OnRun();

	const PString& get_domain();
	const PString& get_passwd();

	void send_sip_rsp(osip_message_t* rsp, const char* ip, const char* port);
	void send_sip_rsp(osip_message_t* rsp, sockaddr_in& in_addr, socklen_t in_addrlen);

	PMediaClient* CreateClient(PString& url);

	PString& get_ip();
	int get_port();

	void add_dialog(sip_dialog* dialog);
	void del_dialog(PString& callid);
	sip_dialog* get_dialog(PString& callid);

private:
	int udp_recv();
	int process_msg(PTaskMsg& msg);

private:
	PString m_ip;
	uint16_t m_port;
	int m_sock;
	PString m_domain;
	PString m_passwd;

	std::map<PString, PSipConn*> m_sipConn;
	std::map<PString, sip_dialog*> m_sipDialog;
};