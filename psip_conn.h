#pragma once

#include "pstring.h"
#include "osipparser2/osip_parser.h"
#include <sys/types.h>          
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

class PSipServer;

class PSipConn
{
public:
	PSipConn(PSipServer* server);

	int process_req(osip_message_t* sip, sockaddr_in& in_addr, socklen_t in_addrlen);

private:
	void clone_basic(osip_message_t* sip, osip_message_t* rsp, int sCode);

private:
	PString m_nonce;
	PSipServer* m_server;
};