#pragma once

#include <pthread.h>
#include "ptask.h"
#include "pstring.h"
#include <map>

class PTaskTimer;

class PManager
{
public:
	static PManager* Instance();

	void AquireLock();
	PTask* GetTask(PString& url);
	void RegistTask(PString& url, PTask* task);
	void UnregistTask(PString& url);
	void ReleaseLock();
	void SetTimer(PTaskTimer* timer);
	PTaskTimer* GetTimer();

private:
	static PManager* m_manager;
	static pthread_mutex_t m_mutex;

	std::map<PString, PTask*> m_urlTask;
	PTaskTimer* m_timer;
};