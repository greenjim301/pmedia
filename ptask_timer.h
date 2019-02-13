#pragma once

#include "ptask.h"
#include <map>

class PTaskTimer : public PTask
{
public:
	void OnRun();

	void RegistTimer(PTask* task, int inter, bool repeat);
	void UnregistTimer(PTask* task);

private:
	struct timer_tick
	{
		int inter;
		bool repeat;
		int tick;
	};

	std::map<PTask*, timer_tick> m_timers;
};