#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

#include "plog.h"
#include "prtsp_server.h"
#include "ptask_timer.h"
#include "pmanager.h"
#include "psip_server.h"

int main(int argc, char *argv[])
{
	PString sipDomain = "34020100002000000001";
	PString sipPwd = "12345678";
	PString sipIP = "192.168.1.155";
	uint16_t sipPort = 5060;
	int rtsp_port = 5544;	

	PLog::Instance()->Init("media.log");
	PManager* pm = PManager::Instance();
	PTaskTimer* timer = new PTaskTimer;	
	
	timer->Start();	
	pm->SetTimer(timer);

	PRtspServer* rtspServer = new PRtspServer(sipIP, rtsp_port);	
	
	rtspServer->Start();

	PSipServer* sipServer = new PSipServer(sipDomain, sipPwd, sipIP, sipPort);

	sipServer->Start();
	pm->SetSipServer(sipServer);

	pm->RunLoop();

	PLog::Instance()->Exit();
	
	return 0;
}
