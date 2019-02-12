#include "pstring.h"

PString::PString() 
	: m_size(0) 
{
	m_buf[0] = '\0';
}

PString::PString(const char* s)
{
	this->assign(s, strlen(s));
}

uint32_t PString::GetMsgSize()
{
	return sizeof(uint32_t) + m_size;
}

const char* PString::c_str() const
{
	return m_buf;
}

char* PString::data()
{
	return m_buf;
}

int PString::size() const
{
	return m_size;
}

bool PString::operator<(const PString& rstr) const
{
	if (m_size == rstr.size())
	{
		int n = memcmp(m_buf, rstr.c_str(), m_size);
		if (n < 0)
		{
			return true; 
		} 
		else
		{
			return false;
		}
	} 
	else
	{
		return m_size < rstr.size();
	}
}

void PString::assign(const char* s, int sSize)
{
	m_size = 0;
	this->append(s, sSize);
}

void PString::append(const char* s, int sSize)
{
	memcpy(m_buf + m_size, s, sSize);
	m_size += sSize;
	m_buf[m_size] = '\0';
}

PString& PString::operator = (const PString& rstr)
{
	this->assign(rstr.c_str(), rstr.size());

	return *this;
}
