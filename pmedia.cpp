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
    if(argc < 3)
    {
        printf("media ip port\n");
        return -1;
    }

	PString ip = argv[1];
	int port = atoi(argv[2]);	

	PLog::Instance()->Init("media.log");

	PTaskTimer* timer = new PTaskTimer;
	
	timer->Start();
	
	PManager::Instance()->SetTimer(timer);

	PRtspServer* rtspServer = new PRtspServer(ip, port);
	
	rtspServer->Start();

	PString sipDomain = "34020100002000000001";
	PString sipPwd = "12345678";
	PString sipIP = "192.168.1.155";
	uint16_t sipPort = 5060;
	PSipServer* sipServer = new PSipServer(sipDomain, sipPwd, sipIP, sipPort);

	sipServer->Start();

	for (;;)
	{
		sleep(60);
	}

	PLog::Instance()->Exit();
	
	return 0;
}
