/*
Plugin of Miranda IM for communicating with users of the MSN Messenger protocol.

Copyright (c) 2012-2017 Miranda NG Team
Copyright (c) 2007-2012 Boris Krasnovskiy.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "msn_proto.h"
#include <m_json.h>

bool CMsnProto::APISkypeComRequest(NETLIBHTTPREQUEST *nlhr, NETLIBHTTPHEADER *headers)
{
	const char *pszSkypeToken = authSkypeToken.XSkypetoken();

	if (!pszSkypeToken) return false;
	nlhr->cbSize = sizeof(NETLIBHTTPREQUEST);
	nlhr->flags = NLHRF_HTTP11 | NLHRF_DUMPASTEXT | NLHRF_PERSISTENT | NLHRF_REDIRECT;
	nlhr->nlc = hHttpsConnection;
	nlhr->headersCount = 3;
	nlhr->headers = headers;
	nlhr->headers[0].szName = "User-Agent";
	nlhr->headers[0].szValue = (char*)MSN_USER_AGENT;
	nlhr->headers[1].szName = "Accept";
	nlhr->headers[1].szValue = "application/json";
	nlhr->headers[2].szName = "X-Skypetoken";
	nlhr->headers[2].szValue = (char*)pszSkypeToken;
	return true;
}

static wchar_t* get_json_str(JSONNode *item, const char *pszValue)
{
	if (JSONNode *node = json_get(item, pszValue)) {
		wchar_t *ret = json_as_string(node);
		if (!mir_wstrcmp(ret, L"null")) {
			mir_free(ret);
			return NULL;
		}
		return ret;
	}
	return NULL;
}

bool CMsnProto::MSN_SKYABRefreshClist(void)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[3];
	CMStringA post;
	bool bRet = false;

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_GET;
	nlhr.szUrl = "https://api.skype.com/users/self/contacts";

	// Query addressbook
	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		if (nlhrReply->resultCode == 200 && nlhrReply->pData) {
			JSONROOT root(nlhrReply->pData);
			if (root == NULL) return false;

			JSONNode *items = json_as_array(root), *item;
			for (size_t i = 0; i < json_size(items); i++) {
				int lstId = LIST_FL;
				ptrW nick;

				item = json_at(items, i);
				if (item == NULL)
					break;

				ptrA skypename(mir_u2a(ptrW(json_as_string(json_get(item, "skypename")))));
				ptrA pszNick(mir_u2a(ptrW(get_json_str(item, "fullname"))));
				char szWLId[128];
				mir_snprintf(szWLId, sizeof(szWLId), "%d:%s", NETID_SKYPE, skypename);
				MCONTACT hContact = MSN_HContactFromEmail(szWLId, pszNick, true, false);
				if (hContact) {
					if (!json_as_bool(json_get(item, "authorized"))) lstId = LIST_PL;
					if (!json_as_bool(json_get(item, "blocked"))) lstId = LIST_BL;
					Lists_Add(lstId, NETID_SKYPE, skypename, NULL, pszNick, NULL);
					post.AppendFormat("contacts[]=%s&", skypename);
				}
			}
			bRet = true;
			json_delete(items);
			MSN_SKYABGetProfiles((const char*)post);
		}
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
	}
	else hHttpsConnection = NULL;
	return bRet;
}

// pszPOST = contacts[]={skypename1}&contacts[]={skypename2}&...
bool CMsnProto::MSN_SKYABGetProfiles(const char *pszPOST)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_POST;
	nlhr.szUrl = "https://api.skype.com/users/self/contacts/profiles";
	nlhr.dataLength = (int)mir_strlen(pszPOST);
	nlhr.pData = (char*)pszPOST;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		if (nlhrReply->resultCode == 200 && nlhrReply->pData) {
			JSONROOT root(nlhrReply->pData);
			if (root == NULL) return false;

			JSONNode *items = json_as_array(root), *item, *node;
			for (size_t i = 0; i < json_size(items); i++) {
				item = json_at(items, i);
				if (item == NULL)
					break;

				node = json_get(item, "username");
				ptrA skypename(mir_u2a(ptrW(json_as_string(node))));
				ptrW value;
				char szWLId[128];
				mir_snprintf(szWLId, sizeof(szWLId), "%d:%s", NETID_SKYPE, skypename);
				MCONTACT hContact = MSN_HContactFromEmail(szWLId, skypename, false, false);

				if (hContact) {
					if (value = get_json_str(item, "firstname")) setWString(hContact, "FirstName", value);
					if (value = get_json_str(item, "lastname")) setWString(hContact, "LastName", value);
					if (value = get_json_str(item, "displayname")) setWString(hContact, "Nick", value);
					if (value = get_json_str(item, "country")) setString(hContact, "Country", (char*)CallService(MS_UTILS_GETCOUNTRYBYISOCODE, (WPARAM)(char*)_T2A(value), 0));
					if (value = get_json_str(item, "city")) setWString(hContact, "City", value);
					if (value = get_json_str(item, "mood")) db_set_ws(hContact, "CList", "StatusMsg", value);
				}
			}
			json_delete(items);
			bRet = true;
		}
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
	}
	else hHttpsConnection = NULL;
	return bRet;
}

bool CMsnProto::MSN_SKYABGetProfile(const char *wlid)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;
	char szURL[256];

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_GET;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/users/%s/profile", wlid);
	nlhr.szUrl = szURL;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		if (nlhrReply->resultCode == 200 && nlhrReply->pData) {
			JSONROOT item(nlhrReply->pData);
			if (item == NULL)
				return false;

			ptrA skypename(mir_u2a(ptrW(json_as_string(json_get(item, "username")))));
			ptrW value;
			char szWLId[128];
			mir_snprintf(szWLId, sizeof(szWLId), "%d:%s", NETID_SKYPE, skypename);
			MCONTACT hContact = MSN_HContactFromEmail(szWLId, skypename, false, false);

			if (hContact) {
				if (value = get_json_str(item, "firstname")) setWString(hContact, "FirstName", value);
				if (value = get_json_str(item, "lastname")) setWString(hContact, "LastName", value);
				if (value = get_json_str(item, "displayname")) setWString(hContact, "Nick", value);
				if (value = get_json_str(item, "gender")) setByte(hContact, "Gender", (BYTE)(_wtoi(value) == 1 ? 'M' : 'F'));
				if (value = get_json_str(item, "birthday")) {
					int d, m, y;
					swscanf(value, L"%d-%d-%d", &y, &m, &d);
					setWord(hContact, "BirthYear", y);
					setByte(hContact, "BirthDay", d);
					setByte(hContact, "BirthMonth", m);
				}
				if (value = get_json_str(item, "country")) setString(hContact, "Country", (char*)CallService(MS_UTILS_GETCOUNTRYBYISOCODE, (WPARAM)(char*)_T2A(value), 0));
				if (value = get_json_str(item, "province")) setWString(hContact, "State", value);
				if (value = get_json_str(item, "city")) setWString(hContact, "City", value);
				if (value = get_json_str(item, "homepage")) setWString(hContact, "Homepage", value);
				if (value = get_json_str(item, "about")) setWString(hContact, "About", value);

				JSONNode *node = json_get(item, "emails");
				if (node && !node->empty()) {
					int num = 0;
					for (auto it = node->begin(); it != node->end(); ++it, ++num) {
						if (num == 3)
							break;

						char szName[16];
						sprintf(szName, "e-mail%d", num);
						setStringUtf(hContact, szName, (*it).as_string().c_str());
					}
				}
				if (value = get_json_str(item, "phoneMobile")) setWString(hContact, "Cellular", value);
				if (value = get_json_str(item, "phone")) setWString(hContact, "Phone", value);
				if (value = get_json_str(item, "phoneOffice")) setWString(hContact, "CompanyPhone", value);
				if (value = get_json_str(item, "mood")) db_set_ws(hContact, "CList", "StatusMsg", value);
			}
			bRet = true;
		}
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
	}
	else hHttpsConnection = NULL;
	return bRet;
}

// pszAction: "block" or "unblock"
bool CMsnProto::MSN_SKYABBlockContact(const char *wlid, const char *pszAction)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;
	char szURL[256], szPOST[128];

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_PUT;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/users/self/contacts/%s/%s", wlid, pszAction);
	nlhr.szUrl = szURL;
	nlhr.headers[3].szName = "Content-type";
	nlhr.headers[3].szValue = "application/x-www-form-urlencoded";
	nlhr.headersCount++;
	nlhr.dataLength = mir_snprintf(szPOST, sizeof(szPOST), "reporterIp=123.123.123.123&uiVersion=%s", msnProductVer);
	nlhr.pData = szPOST;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
		bRet = true;
	}
	else hHttpsConnection = NULL;
	return bRet;
}

bool CMsnProto::MSN_SKYABDeleteContact(const char *wlid)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;
	char szURL[256];

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_DELETE;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/users/self/contacts/%s", wlid);
	nlhr.szUrl = szURL;
	nlhr.headers[3].szName = "Content-type";
	nlhr.headers[3].szValue = "application/x-www-form-urlencoded";
	nlhr.headersCount++;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
		bRet = true;
	}
	else hHttpsConnection = NULL;
	return bRet;
}

// pszAction: "accept" or "decline"
bool CMsnProto::MSN_SKYABAuthRsp(const char *wlid, const char *pszAction)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[3];
	bool bRet = false;
	char szURL[256];

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_PUT;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/users/self/contacts/auth-request/%s/%s", wlid, pszAction);
	nlhr.szUrl = szURL;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
		bRet = true;
	}
	else hHttpsConnection = NULL;
	return bRet;
}

bool CMsnProto::MSN_SKYABAuthRq(const char *wlid, const char *pszGreeting)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;
	char szURL[256];
	CMStringA post;

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) return false;
	nlhr.requestType = REQUEST_PUT;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/users/self/contacts/auth-request/%s", wlid);
	nlhr.szUrl = szURL;
	nlhr.headers[3].szName = "Content-type";
	nlhr.headers[3].szValue = "application/x-www-form-urlencoded";
	nlhr.headersCount++;
	post.Format("greeting=%s", pszGreeting);
	nlhr.dataLength = (int)mir_strlen(post);
	nlhr.pData = (char*)(const char*)post;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
		bRet = true;
	}
	else hHttpsConnection = NULL;
	return bRet;
}

bool CMsnProto::MSN_SKYABSearch(const char *keyWord, HANDLE hSearch)
{
	NETLIBHTTPREQUEST nlhr = { 0 };
	NETLIBHTTPHEADER headers[4];
	bool bRet = false;
	char szURL[256];

	// initialize the netlib request
	if (!APISkypeComRequest(&nlhr, headers)) {
		ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, hSearch, 0);
		return false;
	}
	nlhr.requestType = REQUEST_GET;
	mir_snprintf(szURL, sizeof(szURL), "https://api.skype.com/search/users/any?keyWord=%s&contactTypes[]=skype", keyWord);
	nlhr.szUrl = szURL;
	nlhr.headers[3].szName = "Connection";
	nlhr.headers[3].szValue = "keep-alive";
	nlhr.headersCount++;

	mHttpsTS = clock();
	NETLIBHTTPREQUEST *nlhrReply = (NETLIBHTTPREQUEST*)CallService(MS_NETLIB_HTTPTRANSACTION, (WPARAM)hNetlibUserHttps, (LPARAM)&nlhr);
	mHttpsTS = clock();
	if (nlhrReply) {
		hHttpsConnection = nlhrReply->nlc;
		if (nlhrReply->resultCode == 200 && nlhrReply->pData) {
			JSONROOT root(nlhrReply->pData);
			if (root == NULL) {
				ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, hSearch, 0);
				return false;
			}

			JSONNode *items = json_as_array(root);
			for (size_t i = 0; i < json_size(items); i++) {
				JSONNode *item = json_at(items, i);
				JSONNode *ContactCards = json_get(item, "ContactCards");
				JSONNode *Skype = json_get(ContactCards, "Skype");

				wchar_t *sDisplayName = json_as_string(json_get(Skype, "DisplayName"));
				wchar_t *sSkypeName = json_as_string(json_get(Skype, "SkypeName"));

				PROTOSEARCHRESULT psr = { sizeof(psr) };
				psr.flags = PSR_UNICODE;
				psr.id.w = sSkypeName;
				psr.nick.w = sDisplayName;
				ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_DATA, hSearch, (LPARAM)&psr);
			}
			json_free(items);
			ProtoBroadcastAck(0, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, hSearch, 0);
			bRet = true;
		}
		CallService(MS_NETLIB_FREEHTTPREQUESTSTRUCT, 0, (LPARAM)nlhrReply);
	}
	else hHttpsConnection = NULL;
	return bRet;
}

/*
class GetContactsInfoRequest : public HttpRequest
{
public:
	GetContactsInfoRequest(const char *token, const LIST<char> &skypenames, const char *skypename = "self") :
		HttpRequest(REQUEST_POST, FORMAT, "api.skype.com/users/%s/contacts/profiles", skypename)
	{
		Headers
			<< CHAR_VALUE("X-Skypetoken", token)
			<< CHAR_VALUE("Accept", "application/json");

		for (int i = 0; i < skypenames.getCount(); i++)
			Body << CHAR_VALUE("contacts[]", skypenames[i]);
	}
};

class GetContactStatusRequest : public HttpRequest
{
public:
	GetContactStatusRequest(const char *regToken, const char *skypename, const char *server = SKYPE_ENDPOINTS_HOST) :
		HttpRequest(REQUEST_GET, FORMAT, "%s/v1/users/ME/contacts/8:%s/presenceDocs/messagingService", server, skypename)
	{
		Headers
			<< CHAR_VALUE("Accept", "application/json, text/javascript")
			<< FORMAT_VALUE("RegistrationToken", "registrationToken=%s", regToken);
	}
};

*/
