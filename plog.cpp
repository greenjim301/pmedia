#include "plog.h"
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

PLog* PLog::m_log = NULL;
pthread_mutex_t PLog::m_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t PLog::m_cond = PTHREAD_COND_INITIALIZER;

char* PLog::GetLogBuf()
{
	if (m_logBuf.size())
	{
		char* buf = m_logBuf.front();
		m_logBuf.pop();
		return buf;
	}
	else
	{
		char* buf = (char*)malloc(1024);
		return buf;
	}
}

void PLog::RelLogBuf(char* buf)
{
	if (m_logBuf.size() > 1024)
	{
		free(buf);
	}
	else
	{
		m_logBuf.push(buf);
	}
}

void PLog::EnqueLog(char* log)
{
	m_logQue.push(log);

	pthread_cond_signal(&PLog::m_cond);
}

void PLog::GetLog(std::vector<char*>& logs)
{
	while (m_logQue.size())
	{
		logs.push_back(m_logQue.front());
		m_logQue.pop();
	}
}

void PLog::ProcessLog(std::vector<char*>& logs)
{
	for (auto it = logs.begin(); it != logs.end(); ++it)
	{
		char* log = *it;
		time_t tt;
		struct tm atm;

		time(&tt);
		localtime_r(&tt, &atm);

		fprintf(m_fp, "%04d-%02d-%02d %02d:%02d:%02d ", 
			atm.tm_year + 1900, atm.tm_mon + 1, atm.tm_mday, atm.tm_hour, atm.tm_min, atm.tm_sec);
		fprintf(m_fp, log);
		fflush(m_fp);
	}
}

void PLog::RelLog(std::vector<char*>& logs)
{
	for (auto it = logs.begin(); it != logs.end(); ++it)
	{
		this->RelLogBuf(*it);
	}

	logs.clear();
}

PLog* PLog::Instance()
{
	if (PLog::m_log)
	{
		return PLog::m_log;
	}
	else
	{
		pthread_mutex_lock(&PLog::m_mutex);

		if (PLog::m_log)
		{
			pthread_mutex_unlock(&PLog::m_mutex);
			return PLog::m_log;
		}
		else
		{
			PLog::m_log = new PLog;
			pthread_mutex_unlock(&PLog::m_mutex);
			return PLog::m_log;
		}
	}
}

static void * log_start(void *arg)
{
	PLog* log = static_cast<PLog*>(arg);
	log->OnRun();
	delete log;
	return NULL;
}

PLog::PLog()
	: m_fp(NULL)
	, m_exit(false)
{

}

PLog::~PLog()
{
	if (m_fp)
	{
		fclose(m_fp);
	}

	while (m_logQue.size())
	{
		char* log = m_logQue.front();
		free(log);
		m_logQue.pop();
	}

	while (m_logBuf.size())
	{
		char* log = m_logBuf.front();
		free(log);
		m_logBuf.pop();
	}
}

void PLog::Init(const char* logFile)
{
	pthread_mutex_lock(&PLog::m_mutex);

	m_logFile = logFile;
	pthread_t tid;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);   
    
	int ret = pthread_create(&tid, &attr, log_start, this);
	if (ret)
	{
		printf("create log thread err:%d\n", ret);
		pthread_mutex_unlock(&PLog::m_mutex);

	}
	else
	{
		pthread_cond_wait(&PLog::m_cond, &PLog::m_mutex);
		pthread_mutex_unlock(&PLog::m_mutex);
	}
    
    pthread_attr_destroy(&attr);
}

void PLog::Exit()
{
	pthread_mutex_lock(&PLog::m_mutex);
	m_exit = true;
	pthread_cond_signal(&PLog::m_cond);
	pthread_mutex_unlock(&PLog::m_mutex);
}

void PLog::OnRun()
{
	pthread_cond_signal(&PLog::m_cond);

	m_fp = fopen(m_logFile.c_str(), "a");
	if (!m_fp)
	{
		printf("open %s err:%d\n", m_logFile.c_str(), errno);
		return;
	}

	std::vector<char*> logs;

	pthread_mutex_lock(&PLog::m_mutex);

	while (!m_exit)
	{
		//release lock
		pthread_cond_wait(&PLog::m_cond, &PLog::m_mutex);
		
		//aquire lock
		while (m_logQue.size())
		{
			this->GetLog(logs);

			//release lock
			pthread_mutex_unlock(&PLog::m_mutex);

			//slow write disk
			this->ProcessLog(logs);

			//aquire lock
			pthread_mutex_lock(&PLog::m_mutex);

			//release log buffer
			this->RelLog(logs);
		}		
	}

	pthread_mutex_unlock(&PLog::m_mutex);
}

void PLog::Log(const char* strLog, ...)
{
	pthread_mutex_lock(&PLog::m_mutex);

	if (m_exit)
	{
		pthread_mutex_unlock(&PLog::m_mutex);
		return;
	}

	char* logBuf = this->GetLogBuf();
	int maxLen = 1024 - 1;

	va_list	argList;

	va_start(argList, strLog);

	int nRet = vsnprintf(logBuf, maxLen, strLog, argList);

	va_end(argList);

	if (nRet < 0)
	{
		this->RelLogBuf(logBuf);
	}
	else 
	{
		if (nRet > maxLen - 1)
		{
			nRet = maxLen - 1;
		}

		logBuf[nRet] = '\n';
		logBuf[nRet + 1] = '\0';

		this->EnqueLog(logBuf);
	}

	pthread_mutex_unlock(&PLog::m_mutex);
}
