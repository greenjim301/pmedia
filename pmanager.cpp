#include "pmanager.h"

PManager* PManager::m_manager = NULL;
pthread_mutex_t PManager::m_mutex = PTHREAD_MUTEX_INITIALIZER;

PManager* PManager::Instance()
{
	if (PManager::m_manager)
	{
		return PManager::m_manager;
	}
	else
	{
		pthread_mutex_lock(&PManager::m_mutex);

		if (PManager::m_manager)
		{
			pthread_mutex_unlock(&PManager::m_mutex);
			return PManager::m_manager;
		}
		else
		{
			PManager::m_manager = new PManager;
			pthread_mutex_unlock(&PManager::m_mutex);
			return PManager::m_manager;
		}
	}
}

void PManager::AquireLock()
{
	pthread_mutex_lock(&PManager::m_mutex);
}

PTask* PManager::GetTask(PString& url)
{
	auto it = m_urlTask.find(url);

	if (it != m_urlTask.end())
	{
		return it->second;
	}
	else
	{
		return NULL;
	}
}

void PManager::RegistTask(PString& url, PTask* task)
{
	m_urlTask[url] = task;
}

void PManager::UnregistTask(PString& url)
{
	m_urlTask.erase(url);
}

void PManager::ReleaseLock()
{
	pthread_mutex_unlock(&PManager::m_mutex);
}

void PManager::SetTimer(PTaskTimer* timer)
{
	m_timer = timer;
}

PTaskTimer* PManager::GetTimer()
{
	return m_timer;
}
