#pragma once

#include <stdint.h>
#include <string.h>

class PString
{
public:
	PString();
	PString(const char* s);

	uint32_t GetMsgSize();

	const char* c_str() const;
	char* data();
	int size() const;

	PString& operator = (const PString& rstr);
	bool operator < (const PString& rstr) const;

	void assign(const char* s, int sSize);
	void append(const char* s, int sSize);

private:	
    uint32_t m_size;
	char   m_buf[256];
};
