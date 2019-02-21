#pragma once

#include <pthread.h>
#include "pmedia_client.h"
#include "pstring.h"
#include <map>

class PTaskTimer;
class PSipServer;

class PManager
{
public:
	static PManager* Instance();

	PManager();

	void AquireLock();
	PMediaClient* GetMediaClient(PRtspConn* rtspConn, int pro, PString& url);
	void RegistClient(PString& url, PMediaClient* task);
	void UnregistClient(PString& url);
	void ReleaseLock();
	void SetTimer(PTaskTimer* timer);
	PTaskTimer* GetTimer();
	
	void SetSipServer(PSipServer* s);

	int CreateUdpSock(int& out_sock, PString& out_ip, uint16_t& out_port);

private:
	static PManager* m_manager;
	static pthread_mutex_t m_mutex;

	std::map<PString, PMediaClient*> m_urlClient;
	PTaskTimer* m_timer;
	PSipServer* m_sipServer;

	PString m_rtpIP;
	uint16_t m_curPort;
	uint16_t m_minPort;
	uint16_t m_maxPort;
};