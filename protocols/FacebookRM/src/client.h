/*

Facebook plugin for Miranda Instant Messenger
_____________________________________________

Copyright � 2009-11 Michal Zelinka, 2011-17 Robert P�sel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "constants.h"

#pragma once

#define FORCE_DISCONNECT 1
#define FORCE_QUIT 2

class facebook_client
{
public:

	////////////////////////////////////////////////////////////

	// Client definition

	facebook_client()
	{
		msgid_ = error_count_ = last_feeds_update_ = last_notification_time_ = random_ = chat_msgs_recv_ = chat_req_ = 0;

		send_message_lock_ = notifications_lock_ = cookies_lock_ = loading_history_lock_ = NULL;
		hChannelCon = NULL;
		hMessagesCon = NULL;
		hFcbCon = NULL;
		fcb_conn_lock_ = NULL;
		handle_ = NULL;
		parent = NULL;

		mbasicWorks = true;
		loading_history = false;
	}

	HANDLE hChannelCon;
	HANDLE hMessagesCon;
	HANDLE hFcbCon;
	HANDLE fcb_conn_lock_;

	// Random generator value for this client

	unsigned int random_;

	// Parent handle

	FacebookProto*  parent;

	// User data

	facebook_user   self_;

	std::string username_;
	std::string password_;

	std::string dtsg_;
	std::string ttstamp_;
	std::string logout_hash_;
	std::string chat_channel_;
	std::string chat_channel_host_;
	std::string chat_channel_partition_;
	std::string chat_sequence_num_;
	std::string chat_reconnect_reason_;
	std::string chat_sticky_num_;
	std::string chat_sticky_pool_;
	std::string chat_conn_num_;
	std::string chat_clientid_;
	std::string chat_traceid_;
	time_t last_feeds_update_;
	time_t last_notification_time_;
	volatile unsigned int msgid_;
	int chat_msgs_recv_;
	volatile unsigned int chat_req_;

	bool mbasicWorks;

	////////////////////////////////////////////////////////////

	// Client vs protocol communication

	void    client_notify(wchar_t* message);

	////////////////////////////////////////////////////////////

	// Cookies, Data storage

	HANDLE cookies_lock_;

	std::map<std::string, std::string> cookies;
	std::map<std::string, std::string> pages;
	std::map<std::string, facebook_chatroom*> chat_rooms;
	std::map<std::string, facebook_notification*> notifications;

	// Contact/thread id caches
	std::map<std::string, std::string> thread_id_to_user_id;
	std::map<std::string, MCONTACT> chat_id_to_hcontact;
	std::map<std::string, MCONTACT> user_id_to_hcontact;

	std::string get_newsfeed_type();
	std::string get_server_type();
	std::string get_privacy_type();

	std::set<MCONTACT> ignore_read;
	std::set<MCONTACT> typers;		// store info about typing contacts, because Facebook doesn't send "stopped typing" event when there is actual message being sent
	std::map<MCONTACT, time_t> readers;

	char*   load_cookies();
	void    store_headers(http::response* resp, NETLIBHTTPHEADER* headers, int headers_count);
	void    clear_cookies();
	void	clear_notifications();
	void	clear_chatrooms();
	void	clear_readers();
	void	insert_reader(MCONTACT, time_t, const std::string &readerId = "");
	void	erase_reader(MCONTACT);

	////////////////////////////////////////////////////////////

	// Connection handling

	unsigned int error_count_;

	bool    handle_entry(const std::string &method);
	bool    handle_success(const std::string &method);
	bool    handle_error(const std::string &method, int force_disconnect = 0);

	void __inline increment_error() { this->error_count_++; }
	void __inline decrement_error() { if (error_count_ > 0) error_count_--; }
	void __inline reset_error() { error_count_ = 0; }

	////////////////////////////////////////////////////////////

	// Helpers for data

	__inline const char *__dyn() {
		return ""; // FIXME: What's this value and where it come from? Looks like it is the same through all requests.
	}

	__inline const char *__req() {
		// Increment request number and convert it to string with radix 36 (whole numbers + whole alphabet)
		char buffer[10];
		itoa(InterlockedIncrement(&this->chat_req_), buffer, 36);
		return CMStringA(buffer);
	}

	__inline const char *__rev() {
		return "2509236"; // FIXME: Some version of communication protocol? This version is from 17.8.2016
	}

	////////////////////////////////////////////////////////////

	// Login handling

	bool    login(const char *username, const char *password);
	bool    logout();
	bool	sms_code(const char *fb_dtsg);

	const std::string & get_username() const;

	////////////////////////////////////////////////////////////

	// Session handling

	bool    home();
	bool    reconnect();
	bool    chat_state(bool online = true);

	////////////////////////////////////////////////////////////

	// Updates handling

	List::List<facebook_user> buddies;
	HANDLE  send_message_lock_;
	HANDLE  notifications_lock_;

	////////////////////////////////////////////////////////////

	// Messages handling

	std::map<std::string, int> messages_ignore;
	std::map<int, time_t> messages_timestamp;
	
	bool loading_history;
	HANDLE  loading_history_lock_;

	bool    channel();
	bool	activity_ping();
	int		send_message(int seqid, MCONTACT, const std::string &message_text, std::string *error_text, const std::string &captchaPersistData = "", const std::string &captcha = "");

	////////////////////////////////////////////////////////////

	// Status handling

	bool    post_status(status_data *data);

	////////////////////////////////////////////////////////////

	// HTTP communication

	http::response sendRequest(HttpRequest *request);
	bool save_url(const std::string &url,const std::wstring &filename, HANDLE &nlc);

	////////////////////////////////////////////////////////////

	// Netlib handle

	HANDLE handle_;

	void set_handle(HANDLE h)
	{
		handle_ = h;
	}
};
