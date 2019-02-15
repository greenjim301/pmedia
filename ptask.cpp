#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "plog.h"
#include "ptask.h"

static void * task_start(void *arg)
{
	PTask* task = static_cast<PTask*>(arg);
	task->OnRun();
	task->OnExit();
	task->DelRef();
	return NULL;
}

PTask::PTask()
	: m_ref(1)
	, m_efd(-1)
	, m_exit(false)
{
	pthread_mutex_init(&m_mutex, NULL);
	
	m_eventfd = eventfd(0, EFD_NONBLOCK);;
	m_efd = epoll_create(1);

	this->AddInEvent(m_eventfd);
}

PTask::~PTask()
{
	close(m_efd);
	close(m_eventfd);

	pthread_mutex_destroy(&m_mutex);
}

void PTask::Start()
{
	pthread_t tid;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	int ret = pthread_create(&tid, &attr, task_start, this);
	if (ret)
	{
		P_LOG("start thread err:%d", ret);
	}

	pthread_attr_destroy(&attr);
}

void PTask::OnExit()
{
	pthread_mutex_lock(&m_mutex);
	m_exit = true;
	pthread_mutex_unlock(&m_mutex);
}

void PTask::AddRef()
{
	pthread_mutex_lock(&m_mutex);
	++m_ref;
	pthread_mutex_unlock(&m_mutex);
}

void PTask::DelRef()
{
	pthread_mutex_lock(&m_mutex);
	--m_ref;

	if (m_ref <= 0)
	{
		pthread_mutex_unlock(&m_mutex);
		delete this;
	}
	else
	{
		pthread_mutex_unlock(&m_mutex);
	}
}

int PTask::EnqueMsg(PTaskMsg& msg)
{
	pthread_mutex_lock(&m_mutex);
	
	if (m_exit)
	{
		pthread_mutex_unlock(&m_mutex);
		return -1;
	}

	m_msgQue.push(msg);

	pthread_mutex_unlock(&m_mutex);

	uint64_t c = 1;
	write(m_eventfd, &c, sizeof(c));

	return 0;
}

void PTask::DequeMsg(std::vector<PTaskMsg>& msgs)
{
	pthread_mutex_lock(&m_mutex);

	while (m_msgQue.size())
	{
		msgs.push_back(m_msgQue.front());
		m_msgQue.pop();
	} 

	pthread_mutex_unlock(&m_mutex);

}

int PTask::AddInEvent(int sock)
{
	struct epoll_event ev;

	ev.events = EPOLLIN;
	ev.data.fd = sock;

	int ret = epoll_ctl(m_efd, EPOLL_CTL_ADD, sock, &ev);
	if (ret < 0)
	{
		P_LOG("add in event err:%d", errno);
		return -1;
	}

	return 0;
}

int PTask::WaitEvent(struct epoll_event* outEvents, std::vector<PTaskMsg>& msgs, int timeout)
{
	msgs.clear();

	int ret = epoll_wait(m_efd, m_events, DEF_MAX_EVENTS, timeout);
	int n = 0;

	if (ret > 0)
	{
		for (int i = 0; i < ret; ++i)
		{
			struct epoll_event& ev = m_events[i];

			if (ev.data.fd == m_eventfd)
			{
				uint64_t c;
				if (-1 != read(m_eventfd, &c, sizeof(c)))
				{
					this->DequeMsg(msgs);
				}				
			}
			else
			{
				outEvents[n] = ev;
				++n;
			}
		}
	}
	else if (ret < 0)
	{
		P_LOG("epoll wait err:%d", errno);
		return -1;
	}

	return n;
}

void PTask::aquire_lock()
{
	pthread_mutex_lock(&m_mutex);
}

void PTask::release_lock()
{
	pthread_mutex_unlock(&m_mutex);
}
