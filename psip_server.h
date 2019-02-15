#pragma once

#include "ptask.h"
#include <map>
#include "osipparser2/osip_parser.h"
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pstring.h"

class PSipConn;

class PSipServer : public PTask
{
public:
	PSipServer(PString& domain, PString& pwd, PString& ip, uint16_t port);

	void OnRun();

	const PString& get_domain();
	const PString& get_passwd();

	void send_sip_rsp(osip_message_t* rsp, sockaddr_in& in_addr, socklen_t in_addrlen);

private:
	int udp_recv();
	int process_msg(PTaskMsg& msg);

private:
	PString m_ip;
	uint16_t m_port;
	int m_sock;
	PString m_doman;
	PString m_passwd;

	std::map<PString, PSipConn*> m_sipConn;
};