#include "prtmp_client.h"
#include <stdlib.h>
#include "prtsp_comm.h"
#include "pmanager.h"

PRtmpClient::PRtmpClient(PString& ulr)
	: m_url(ulr)
{

}

void PRtmpClient::GetMediaInfo(PRtspConn* conn)
{
	//
}

int PRtmpClient::on_rtsp_req(PRtspReq& req, PRtspConn* conn)
{
	return 0;
}

void PRtmpClient::OnRun()
{
	RTMP_Init(&m_rtmp);
	char* buf = (char*)malloc(1024 * 1024);
	int size = 1024 * 1024;
	int ret;

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


	while (1)
	{
		ret = RTMP_Read(&m_rtmp, buf, size);

		if (ret <= 0)
		{
			goto end;
		}
	}

end:
	free(buf);
	RTMP_Close(&m_rtmp);
}

void PRtmpClient::OnExit()
{
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
		return -1;
	}
	break;

	default:
		break;
	}

	return 0;
}
