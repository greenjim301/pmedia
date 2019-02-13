#include "ptask_timer.h"
#include <vector>
#include "prtsp_comm.h"

void PTaskTimer::OnRun()
{
	struct epoll_event events[DEF_MAX_EVENTS];
	std::vector<PTaskMsg> msgs;
	std::map<PTask*, timer_tick>::iterator it;
	std::vector<PTask*> removeTask;
	std::vector<PTask*>::iterator itRemv;
	PTaskMsg msg(EN_TASK_TIMER, NULL);
	int ret;

	for (;;)
	{
		ret = this->WaitEvent(events, msgs, 1000);

		if (ret == 0)
		{
			this->aquire_lock();

			for (it = m_timers.begin(); it != m_timers.end(); ++it)
			{
				timer_tick& tt = it->second;

				if (++tt.tick == tt.inter)
				{
					it->first->EnqueMsg(msg);

					if (tt.repeat)
					{
						tt.tick = 0;
					} 
					else
					{
						removeTask.push_back(it->first);
					}
				}
			}

			for (itRemv = removeTask.begin(); itRemv != removeTask.end(); ++itRemv)
			{
				m_timers.erase(*itRemv);
			}

			this->release_lock();

			removeTask.clear();
		}
	}
}

void PTaskTimer::RegistTimer(PTask* task, int inter, bool repeat)
{
	this->aquire_lock();
	m_timers[task] = { inter, repeat, 0 };
	this->release_lock();
}

void PTaskTimer::UnregistTimer(PTask* task)
{
	this->aquire_lock();
	m_timers.erase(task);
	this->release_lock();
}
