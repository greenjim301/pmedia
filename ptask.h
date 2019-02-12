#pragma once

#include <pthread.h>
#include <sys/epoll.h>
#include <vector>
#include <queue>

class PTaskMsg
{
public:
	PTaskMsg()
		: m_type(0)
		, m_body(NULL)
	{
	}

	PTaskMsg(int type, void* body)
		: m_type(type)
		, m_body(body)
	{
	}

	int m_type;
	void* m_body;
};

class PTask
{
public:
	enum {
		DEF_MAX_EVENTS = 3,
	};

	PTask();

	virtual ~PTask();

	void Start();

	virtual void OnRun() = 0;
	virtual void OnExit();

	void AddRef();
	void DelRef();

	int EnqueMsg(PTaskMsg& msg);
	void DequeMsg(std::vector<PTaskMsg>& msgs);

	int AddInEvent(int sock);
	int WaitEvent(struct epoll_event* outEvents, std::vector<PTaskMsg>& msgs);

private:
	int m_ref;
	pthread_mutex_t m_mutex;
	int m_efd;
	struct epoll_event m_events[DEF_MAX_EVENTS];

	std::queue<PTaskMsg> m_msgQue;
	int m_pipe[2];
	bool m_exit;
};
