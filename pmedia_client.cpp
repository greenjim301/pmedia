#include "pmedia_client.h"
#include "prtsp_conn.h"

void PMediaClient::OnExit()
{
	PTaskMsg msg(EN_CLIENT_EXIT, NULL);

	for (auto it = m_addConn.begin(); it != m_addConn.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}

	for (auto it = m_pendPlay.begin(); it != m_pendPlay.end(); ++it)
	{
		(*it)->EnqueMsg(msg);
		(*it)->DelRef();
	}

	PTask::OnExit();
}

void PMediaClient::GetMediaInfo(PRtspConn* conn)
{
	if (m_descbRsp.m_ret == 200)
	{
		conn->on_descb_rsp(m_descbRsp);
	}

	conn->AddRef();
	m_addConn.insert(conn);
}