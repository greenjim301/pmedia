#pragma once

#include <arpa/inet.h>
#include "pstring.h"

class PEncoder
{
public:
	static void Encode(char*& buf, uint16_t& value)
	{
		uint16_t tt = htons(value);
		*(uint16_t*)buf = tt;
		buf += sizeof(uint16_t);
	}

   static void Encode(char*& buf, uint32_t& value)
   {
       uint32_t tt = htonl(value);
       *(uint32_t*)buf = tt;
   	   buf += sizeof(uint32_t);
   }
   
   static void Encode(char*& buf, int32_t& value)
   {
       int32_t tt = htonl(value);
       *(int32_t*)buf = tt;
       buf += sizeof(int32_t);
   }
   
   static void Encode(char*& buf, PString& value)
   {
       Encode(buf, value.m_size);
   	
   	   if(value.m_size == 0)
   	     return;
   	
   	   memcpy(buf, value.m_buf, value.m_size);
   	   buf += value.m_size;
   }
};
