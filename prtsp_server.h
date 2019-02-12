#pragma once

#include "ptask.h"
#include "pstring.h"

class PRtspServer : public PTask
{
public:
	PRtspServer(PString& ip, uint16_t port);
	~PRtspServer();

	void OnRun();

private:
	PString m_ip;
	uint16_t m_port;
	int m_sock;
};
