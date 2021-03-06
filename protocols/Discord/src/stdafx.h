// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include <Windows.h>
#include <Shlwapi.h>
#include <Wincrypt.h>
#include <stdio.h>
#include <direct.h>
#include <time.h>

#include "resource.h"

#include <m_system_cpp.h>
#include <newpluginapi.h>
#include <m_chat.h>
#include <m_clist.h>
#include <m_database.h>
#include <m_folders.h>
#include <m_history.h>
#include <m_langpack.h>
#include <m_message.h>
#include <m_netlib.h>
#include <m_options.h>
#include <m_popup.h>
#include <m_protocols.h>
#include <m_protosvc.h>
#include <m_protoint.h>
#include <m_skin.h>
#include <m_gui.h>
#include <m_system.h>
#include <m_userinfo.h>
#include <m_icolib.h>
#include <m_utils.h>
#include <m_hotkeys.h>
#include <m_json.h>
#include <win2k.h>

extern HINSTANCE g_hInstance;

#include "version.h"
#include "proto.h"

#define DB_KEY_ID        "id"
#define DB_KEY_EMAIL     "Email"
#define DB_KEY_PASSWORD  "Password"
#define DB_KEY_DISCR     "Discriminator"
#define DB_KEY_MFA       "MfaEnabled"
#define DB_KEY_NICK      "Nick"
#define DB_KEY_AVHASH    "AvatarHash"
#define DB_KEY_CHANNELID "ChannelID"
#define DB_KEY_LASTMSGID "LastMessageID"

#define DB_KEY_GROUP    "GroupName"
#define DB_KEYVAL_GROUP L"Discord"

time_t StringToDate(const CMStringW &str);
