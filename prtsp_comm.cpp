#include "prtsp_comm.h"

void skip_space(char*& p)
{
	while (*p != '\0' && *p == ' ')
	{
		++p;
	}
}
