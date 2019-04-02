#include "prtsp_comm.h"
#include <cstdlib>

void skip_space(char*& p)
{
	while (*p != '\0' && *p == ' ')
	{
		++p;
	}
}


void build_sip_msg(osip_message_t*& sip, sip_dialog* dlg, int cseq, const char* method)
{
	osip_message_init(&sip);

	osip_uri_t* uri;
	osip_uri_init(&uri);

	osip_uri_set_scheme(uri, osip_strdup("sip"));
	osip_uri_set_username(uri, osip_strdup(dlg->destUser.c_str()));
	osip_uri_set_host(uri, osip_strdup(dlg->destHost.c_str()));
	osip_uri_set_port(uri, osip_strdup(dlg->destPort.c_str()));

	osip_message_set_method(sip, osip_strdup(method));
	osip_message_set_uri(sip, uri);
	osip_message_set_version(sip, osip_strdup("SIP/2.0"));

	char temp[128];

	sprintf(temp, "%s;branch=z9hG4bK%d", dlg->via.c_str(), std::rand());
	osip_message_set_via(sip, temp);

	osip_message_set_from(sip, dlg->localTag.c_str());
	osip_message_set_to(sip, dlg->remoteTag.c_str());
	osip_message_set_call_id(sip, dlg->callid.c_str());

	sprintf(temp, "%d %s", cseq, method);	
	osip_message_set_cseq(sip, temp);

	osip_message_set_contact(sip, dlg->localContact.c_str());
	osip_message_set_max_forwards(sip, "70");
}