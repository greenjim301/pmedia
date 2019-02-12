#pragma once

#include <arpa/inet.h>
#include "pstring.h"

class PDecoder
{
public:
	static void Decode(char*& buf, uint16_t& value)
	{
		uint16_t tt = *(uint16_t*)buf;
		value = ntohs(tt);
		buf += sizeof(uint16_t);
	}

    static void Decode(char*& buf, uint32_t& value)
    {
        uint32_t tt = *(uint32_t*)buf;
    	value = ntohl(tt);
    	buf += sizeof(uint32_t);
    }
    
    static void Decode(char*& buf, int32_t& value)
    {
        int32_t tt = *(int32_t*)buf;
    	value = ntohl(tt);
    	buf += sizeof(int32_t);
    }
    
    
    static void Decode(char*& buf, PString& value)
    {
        Decode(buf, value.m_size);
    	
    	if(value.m_size == 0)
    	    return;
    	
    	memcpy(value.m_buf, buf, value.m_size);
    	value.m_buf[value.m_size] = '\0';
    	buf += value.m_size;
    }
};
