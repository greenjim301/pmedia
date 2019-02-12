#pragma once

#include <pthread.h>
#include <queue> 
#include <stdio.h>
#include "pstring.h"

class PLog
{
public:
	PLog();
	~PLog();

	static PLog* Instance();

	void Init(const char* logFile);
	void Exit();
	void OnRun();
	void Log(const char* strlog, ...);

private:
	char* GetLogBuf();
	void  RelLogBuf(char* buf);
	void  EnqueLog(char* log);
	void  GetLog(std::vector<char*>& logs);
	void  ProcessLog(std::vector<char*>& logs);
	void  RelLog(std::vector<char*>& logs);

	static PLog* m_log;
	static pthread_mutex_t m_mutex;
	static pthread_cond_t  m_cond;

	FILE* m_fp;
	bool  m_exit;
	PString m_logFile;
	std::queue<char*> m_logBuf;
	std::queue<char*> m_logQue;
};

#define P_LOG(FORMAT_STRING, ...) \
{\
    PLog::Instance()->Log(FORMAT_STRING, ##__VA_ARGS__); \
}			

