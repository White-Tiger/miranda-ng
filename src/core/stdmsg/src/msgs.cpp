/*

Copyright 2000-12 Miranda IM, 2012-17 Miranda NG project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"
#include "statusicon.h"

/* Missing MinGW GUIDs */
#ifdef __MINGW32__
const CLSID IID_IRichEditOle = { 0x00020D00, 0x00, 0x00, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const CLSID IID_IRichEditOleCallback = { 0x00020D03, 0x00, 0x00, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
#endif

int OnCheckPlugins(WPARAM, LPARAM);

HCURSOR  hCurSplitNS, hCurSplitWE, hCurHyperlinkHand;
HANDLE   hHookWinEvt, hHookWinPopup, hHookWinWrite;
HGENMENU hMsgMenuItem;
HMODULE hMsftEdit;

static int SRMMStatusToPf2(int status)
{
	switch (status) {
		case ID_STATUS_ONLINE:     return PF2_ONLINE;
		case ID_STATUS_AWAY:       return PF2_SHORTAWAY;
		case ID_STATUS_DND:        return PF2_HEAVYDND;
		case ID_STATUS_NA:         return PF2_LONGAWAY;
		case ID_STATUS_OCCUPIED:   return PF2_LIGHTDND;
		case ID_STATUS_FREECHAT:   return PF2_FREECHAT;
		case ID_STATUS_INVISIBLE:  return PF2_INVISIBLE;
		case ID_STATUS_ONTHEPHONE: return PF2_ONTHEPHONE;
		case ID_STATUS_OUTTOLUNCH: return PF2_OUTTOLUNCH;
		case ID_STATUS_OFFLINE:    return MODEF_OFFLINE;
	}
	return 0;
}

static int MessageEventAdded(WPARAM hContact, LPARAM lParam)
{
	DBEVENTINFO dbei = { sizeof(dbei) };
	db_event_get(lParam, &dbei);

	if (dbei.flags & (DBEF_SENT | DBEF_READ) || !(dbei.eventType == EVENTTYPE_MESSAGE || DbEventIsForMsgWindow(&dbei)))
		return 0;

	pcli->pfnRemoveEvent(hContact, 1);
	/* does a window for the contact exist? */
	HWND hwnd = WindowList_Find(pci->hWindowList, hContact);
	if (hwnd) {
		if (!g_dat.bDoNotStealFocus) {
			ShowWindow(hwnd, SW_RESTORE);
			SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			SetForegroundWindow(hwnd);
			SkinPlaySound("RecvMsgActive");
		}
		else {
			if (GetForegroundWindow() == hwnd)
				SkinPlaySound("RecvMsgActive");
			else
				SkinPlaySound("RecvMsgInactive");
		}
		return 0;
	}
	/* new message */
	SkinPlaySound("AlertMsg");

	char *szProto = GetContactProto(hContact);
	if (szProto && (g_dat.openFlags & SRMMStatusToPf2(CallProtoService(szProto, PS_GETSTATUS, 0, 0)))) {
		CSrmmWindow *pDlg = new CSrmmWindow(hContact, g_dat.bDoNotStealFocus);
		pDlg->Show();
		return 0;
	}

	wchar_t toolTip[256];
	mir_snwprintf(toolTip, TranslateT("Message from %s"), pcli->pfnGetContactDisplayName(hContact, 0));

	CLISTEVENT cle = {};
	cle.hContact = hContact;
	cle.hDbEvent = lParam;
	cle.flags = CLEF_TCHAR;
	cle.hIcon = Skin_LoadIcon(SKINICON_EVENT_MESSAGE);
	cle.pszService = "SRMsg/ReadMessage";
	cle.ptszTooltip = toolTip;
	pcli->pfnAddEvent(&cle);
	return 0;
}

INT_PTR SendMessageCmd(MCONTACT hContact, char *msg, int isWchar)
{
	/* does the MCONTACT's protocol support IM messages? */
	char *szProto = GetContactProto(hContact);
	if (!szProto || (!CallProtoService(szProto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_IMSEND))
		return 1;

	hContact = db_mc_tryMeta(hContact);

	HWND hwnd = WindowList_Find(pci->hWindowList, hContact);
	if (hwnd) {
		if (msg) {
			SendDlgItemMessage(hwnd, IDC_MESSAGE, EM_SETSEL, -1, SendDlgItemMessage(hwnd, IDC_MESSAGE, WM_GETTEXTLENGTH, 0, 0));
			if (isWchar)
				SendDlgItemMessageW(hwnd, IDC_MESSAGE, EM_REPLACESEL, FALSE, (LPARAM)msg);
			else
				SendDlgItemMessageA(hwnd, IDC_MESSAGE, EM_REPLACESEL, FALSE, (LPARAM)msg);
		}
		ShowWindow(hwnd, SW_RESTORE);
		SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow(hwnd);
	}
	else {
		CSrmmWindow *pDlg = new CSrmmWindow(hContact, false, msg, isWchar != 0);
		pDlg->Show();
	}
	return 0;
}

static INT_PTR SendMessageCommand_W(WPARAM wParam, LPARAM lParam)
{
	return SendMessageCmd(wParam, (char*)lParam, TRUE);
}

static INT_PTR SendMessageCommand(WPARAM wParam, LPARAM lParam)
{
	return SendMessageCmd(wParam, (char*)lParam, FALSE);
}

static INT_PTR ReadMessageCommand(WPARAM, LPARAM lParam)
{
	CLISTEVENT *cle = (CLISTEVENT *)lParam;
	if (cle)
		SendMessageCmd(cle->hContact, NULL, 0);

	return 0;
}

static int TypingMessage(WPARAM hContact, LPARAM lParam)
{
	if (!g_dat.bShowTyping)
		return 0;

	hContact = db_mc_tryMeta(hContact);

	SkinPlaySound((lParam) ? "TNStart" : "TNStop");

	HWND hwnd = WindowList_Find(pci->hWindowList, hContact);
	if (hwnd)
		SendMessage(hwnd, DM_TYPING, 0, lParam);
	else if (lParam && g_dat.bShowTypingTray) {
		wchar_t szTip[256];
		mir_snwprintf(szTip, TranslateT("%s is typing a message"), pcli->pfnGetContactDisplayName(hContact, 0));

		if (g_dat.bShowTypingClist) {
			pcli->pfnRemoveEvent(hContact, 1);

			CLISTEVENT cle = {};
			cle.hContact = hContact;
			cle.hDbEvent = 1;
			cle.flags = CLEF_ONLYAFEW | CLEF_TCHAR;
			cle.hIcon = Skin_LoadIcon(SKINICON_OTHER_TYPING);
			cle.pszService = "SRMsg/ReadMessage";
			cle.ptszTooltip = szTip;
			pcli->pfnAddEvent(&cle);

			IcoLib_ReleaseIcon(cle.hIcon);
		}
		else Clist_TrayNotifyW(NULL, TranslateT("Typing notification"), szTip, NIIF_INFO, 1000 * 4);
	}
	return 0;
}

static int MessageSettingChanged(WPARAM hContact, LPARAM lParam)
{
	DBCONTACTWRITESETTING *cws = (DBCONTACTWRITESETTING*)lParam;
	if (cws->szModule == NULL)
		return 0;

	if (!strcmp(cws->szModule, "CList"))
		WindowList_Broadcast(pci->hWindowList, DM_UPDATETITLE, (WPARAM)cws, 0);
	else if (hContact) {
		if (cws->szSetting && !strcmp(cws->szSetting, "Timezone"))
			WindowList_Broadcast(pci->hWindowList, DM_NEWTIMEZONE, (WPARAM)cws, 0);
		else {
			char *szProto = GetContactProto(hContact);
			if (szProto && !strcmp(cws->szModule, szProto))
				WindowList_Broadcast(pci->hWindowList, DM_UPDATETITLE, (WPARAM)cws, 0);
		}
	}
	return 0;
}

// If a contact gets deleted, close its message window if there is any
static int ContactDeleted(WPARAM wParam, LPARAM)
{
	HWND hwnd = WindowList_Find(pci->hWindowList, wParam);
	if (hwnd)
		SendMessage(hwnd, WM_CLOSE, 0, 0);

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

struct MSavedEvent
{
	MSavedEvent(MCONTACT _hContact, MEVENT _hEvent) :
		hContact(_hContact),
		hEvent(_hEvent)
	{
	}

	MEVENT   hEvent;
	MCONTACT hContact;
};

static void RestoreUnreadMessageAlerts(void)
{
	OBJLIST<MSavedEvent> arEvents(10, NumericKeySortT);

	for (MCONTACT hContact = db_find_first(); hContact; hContact = db_find_next(hContact)) {
		for (MEVENT hDbEvent = db_event_firstUnread(hContact); hDbEvent; hDbEvent = db_event_next(hContact, hDbEvent)) {
			bool autoPopup = false;

			DBEVENTINFO dbei = { sizeof(dbei) };
			dbei.cbBlob = 0;
			db_event_get(hDbEvent, &dbei);
			if (!(dbei.flags & (DBEF_SENT | DBEF_READ)) && (dbei.eventType == EVENTTYPE_MESSAGE || DbEventIsForMsgWindow(&dbei))) {
				int windowAlreadyExists = WindowList_Find(pci->hWindowList, hContact) != NULL;
				if (windowAlreadyExists)
					continue;

				char *szProto = GetContactProto(hContact);
				if (szProto && (g_dat.openFlags & SRMMStatusToPf2(CallProtoService(szProto, PS_GETSTATUS, 0, 0))))
					autoPopup = true;

				if (autoPopup && !windowAlreadyExists) {
					CSrmmWindow *pDlg = new CSrmmWindow(hContact, g_dat.bDoNotStealFocus);
					pDlg->Show();
				}
				else arEvents.insert(new MSavedEvent(hContact, hDbEvent));
			}
		}
	}

	wchar_t toolTip[256];

	CLISTEVENT cle = {};
	cle.hIcon = Skin_LoadIcon(SKINICON_EVENT_MESSAGE);
	cle.pszService = "SRMsg/ReadMessage";
	cle.flags = CLEF_TCHAR;
	cle.ptszTooltip = toolTip;

	for (int i = 0; i < arEvents.getCount(); i++) {
		MSavedEvent &e = arEvents[i];
		mir_snwprintf(toolTip, TranslateT("Message from %s"), pcli->pfnGetContactDisplayName(e.hContact, 0));
		cle.hContact = e.hContact;
		cle.hDbEvent = e.hEvent;
		pcli->pfnAddEvent(&cle);
	}
}

void RegisterSRMMFonts(void);

/////////////////////////////////////////////////////////////////////////////////////////
// toolbar buttons support

int RegisterToolbarIcons(WPARAM, LPARAM)
{
	BBButton bbd = {};
	bbd.pszModuleName = "SRMM";
	bbd.bbbFlags = BBBF_ISIMBUTTON | BBBF_CREATEBYID;

	bbd.dwButtonID = IDC_ADD;
	bbd.dwDefPos = 10;
	bbd.hIcon = Skin_GetIconHandle(SKINICON_OTHER_ADDCONTACT);
	bbd.pwszText = LPGENW("&Add");
	bbd.pwszTooltip = LPGENW("Add contact permanently to list");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_USERMENU;
	bbd.dwDefPos = 20;
	bbd.hIcon = Skin_GetIconHandle(SKINICON_OTHER_DOWNARROW);
	bbd.pwszText = LPGENW("&User menu");
	bbd.pwszTooltip = LPGENW("User menu");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_DETAILS;
	bbd.dwDefPos = 30;
	bbd.hIcon = Skin_GetIconHandle(SKINICON_OTHER_USERDETAILS);
	bbd.pwszText = LPGENW("User &details");
	bbd.pwszTooltip = LPGENW("View user's details");
	Srmm_AddButton(&bbd);

	bbd.bbbFlags |= BBBF_ISCHATBUTTON | BBBF_ISRSIDEBUTTON;
	bbd.dwButtonID = IDC_HISTORY;
	bbd.dwDefPos = 40;
	bbd.hIcon = Skin_GetIconHandle(SKINICON_OTHER_HISTORY);
	bbd.pwszText = LPGENW("&History");
	bbd.pwszTooltip = LPGENW("View user's history (CTRL+H)");
	Srmm_AddButton(&bbd);

	// chat buttons
	bbd.bbbFlags = BBBF_ISPUSHBUTTON | BBBF_ISCHATBUTTON | BBBF_CREATEBYID;
	bbd.dwButtonID = IDC_BOLD;
	bbd.dwDefPos = 10;
	bbd.hIcon = GetIconHandle("bold");
	bbd.pwszText = LPGENW("&Bold");
	bbd.pwszTooltip = LPGENW("Make the text bold (CTRL+B)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_ITALICS;
	bbd.dwDefPos = 15;
	bbd.hIcon = GetIconHandle("italics");
	bbd.pwszText = LPGENW("&Italic");
	bbd.pwszTooltip = LPGENW("Make the text italicized (CTRL+I)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_UNDERLINE;
	bbd.dwDefPos = 20;
	bbd.hIcon = GetIconHandle("underline");
	bbd.pwszText = LPGENW("&Underline");
	bbd.pwszTooltip = LPGENW("Make the text underlined (CTRL+U)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_COLOR;
	bbd.dwDefPos = 25;
	bbd.hIcon = GetIconHandle("fgcol");
	bbd.pwszText = LPGENW("&Color");
	bbd.pwszTooltip = LPGENW("Select a foreground color for the text (CTRL+K)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_BKGCOLOR;
	bbd.dwDefPos = 30;
	bbd.hIcon = GetIconHandle("bkgcol");
	bbd.pwszText = LPGENW("&Background color");
	bbd.pwszTooltip = LPGENW("Select a background color for the text (CTRL+L)");
	Srmm_AddButton(&bbd);

	bbd.bbbFlags = BBBF_ISCHATBUTTON | BBBF_ISRSIDEBUTTON | BBBF_CREATEBYID;
	bbd.dwButtonID = IDC_CHANMGR;
	bbd.dwDefPos = 30;
	bbd.hIcon = GetIconHandle("settings");
	bbd.pwszText = LPGENW("&Room settings");
	bbd.pwszTooltip = LPGENW("Control this room (CTRL+O)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_SHOWNICKLIST;
	bbd.dwDefPos = 20;
	bbd.hIcon = GetIconHandle("nicklist");
	bbd.pwszText = LPGENW("&Show/hide nick list");
	bbd.pwszTooltip = LPGENW("Show/hide the nick list (CTRL+N)");
	Srmm_AddButton(&bbd);

	bbd.dwButtonID = IDC_FILTER;
	bbd.dwDefPos = 10;
	bbd.hIcon = GetIconHandle("filter");
	bbd.pwszText = LPGENW("&Filter");
	bbd.pwszTooltip = LPGENW("Enable/disable the event filter (CTRL+F)");
	Srmm_AddButton(&bbd);
	return 0;
}

void SetButtonsPos(HWND hwndDlg, bool bIsChat)
{
	HDWP hdwp = BeginDeferWindowPos(Srmm_GetButtonCount());

	int yPos;
	RECT rc;
	if (bIsChat) {
		GetWindowRect(GetDlgItem(hwndDlg, IDC_SPLITTERY), &rc);
		POINT pt = { 0, rc.top };
		ScreenToClient(hwndDlg, &pt);
		yPos = pt.y - 2;
	}
	else yPos = 2;

	GetClientRect(hwndDlg, &rc);
	int iLeftX = 2, iRightX = rc.right - 2;

	CustomButtonData *cbd;
	for (int i = 0; cbd = Srmm_GetNthButton(i); i++) {
		HWND hwndButton = GetDlgItem(hwndDlg, cbd->m_dwButtonCID);
		if (hwndButton == NULL || cbd->m_bHidden)
			continue;

		if (cbd->m_bRSided) {
			iRightX -= g_dat.iGap + cbd->m_iButtonWidth;
			hdwp = DeferWindowPos(hdwp, hwndButton, NULL, iRightX, yPos, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
		}
		else {
			hdwp = DeferWindowPos(hdwp, hwndButton, NULL, iLeftX, yPos, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
			iLeftX += g_dat.iGap + cbd->m_iButtonWidth;
		}
	}

	EndDeferWindowPos(hdwp);
}

/////////////////////////////////////////////////////////////////////////////////////////

static int FontsChanged(WPARAM, LPARAM)
{
	WindowList_Broadcast(pci->hWindowList, DM_OPTIONSAPPLIED, TRUE, 0);
	return 0;
}

static int SplitmsgModulesLoaded(WPARAM, LPARAM)
{
	RegisterSRMMFonts();
	LoadMsgLogIcons();
	OnCheckPlugins(0, 0);

	CMenuItem mi;
	SET_UID(mi, 0x58d8dc1, 0x1c25, 0x49c0, 0xb8, 0x7c, 0xa3, 0x22, 0x2b, 0x3d, 0xf1, 0xd8);
	mi.position = -2000090000;
	mi.flags = CMIF_DEFAULT;
	mi.hIcolibItem = Skin_GetIconHandle(SKINICON_EVENT_MESSAGE);
	mi.name.a = LPGEN("&Message");
	mi.pszService = MS_MSG_SENDMESSAGE;
	hMsgMenuItem = Menu_AddContactMenuItem(&mi);

	HookEvent(ME_FONT_RELOAD, FontsChanged);
	HookEvent(ME_MSG_TOOLBARLOADED, RegisterToolbarIcons);

	RestoreUnreadMessageAlerts();
	return 0;
}

int PreshutdownSendRecv(WPARAM, LPARAM)
{
	WindowList_Broadcast(pci->hWindowList, WM_CLOSE, 0, 0);

	DeinitStatusIcons();
	return 0;
}

static int IconsChanged(WPARAM, LPARAM)
{
	FreeMsgLogIcons();
	LoadMsgLogIcons();

	// change all the icons
	WindowList_Broadcast(pci->hWindowList, DM_REMAKELOG, 0, 0);
	WindowList_Broadcast(pci->hWindowList, DM_UPDATEWINICON, 0, 0);
	return 0;
}

static int PrebuildContactMenu(WPARAM hContact, LPARAM)
{
	if (hContact) {
		bool bEnabled = false;
		char *szProto = GetContactProto(hContact);
		if (szProto) {
			// leave this menu item hidden for chats
			if (!db_get_b(hContact, szProto, "ChatRoom", 0))
				if (CallProtoService(szProto, PS_GETCAPS, PFLAGNUM_1, 0) & PF1_IMSEND)
					bEnabled = true;
		}

		Menu_ShowItem(hMsgMenuItem, bEnabled);
	}
	return 0;
}

static INT_PTR GetWindowAPI(WPARAM, LPARAM)
{
	return PLUGIN_MAKE_VERSION(0, 0, 0, 4);
}

static INT_PTR GetWindowClass(WPARAM wParam, LPARAM lParam)
{
	char *szBuf = (char*)wParam;
	int size = (int)lParam;
	mir_snprintf(szBuf, size, SRMMMOD);
	return 0;
}

static INT_PTR SetStatusText(WPARAM wParam, LPARAM lParam)
{
	HWND hwnd = WindowList_Find(pci->hWindowList, wParam);
	if (hwnd == NULL)
		return 1;

	CSrmmWindow *dat = (CSrmmWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (dat == NULL)
		return 1;

	StatusTextData *st = (StatusTextData*)lParam;
	if (st != NULL && st->cbSize != sizeof(StatusTextData))
		return 1;

	dat->SetStatusData(st);
	return 0;
}

static INT_PTR GetWindowData(WPARAM wParam, LPARAM lParam)
{
	MessageWindowInputData *mwid = (MessageWindowInputData*)wParam;
	if (mwid == NULL || (mwid->cbSize != sizeof(MessageWindowInputData)) || (mwid->hContact == NULL) || (mwid->uFlags != MSG_WINDOW_UFLAG_MSG_BOTH))
		return 1;

	MessageWindowData *mwd = (MessageWindowData*)lParam;
	if(mwd == NULL || (mwd->cbSize != sizeof(MessageWindowData)))
		return 1;

	HWND hwnd = WindowList_Find(pci->hWindowList, mwid->hContact);
	mwd->uFlags = MSG_WINDOW_UFLAG_MSG_BOTH;
	mwd->hwndWindow = hwnd;
	mwd->local = 0;
	mwd->uState = MSG_WINDOW_STATE_EXISTS;
	if (IsWindowVisible(hwnd))
		mwd->uState |= MSG_WINDOW_STATE_VISIBLE;
	if (GetForegroundWindow() == hwnd)
		mwd->uState |= MSG_WINDOW_STATE_FOCUS;
	if (IsIconic(hwnd))
		mwd->uState |= MSG_WINDOW_STATE_ICONIC;
	return 0;
}

static wchar_t tszError[] = LPGENW("Miranda could not load the built-in message module, msftedit.dll is missing. Press 'Yes' to continue loading Miranda.");

int LoadSendRecvMessageModule(void)
{
	if ((hMsftEdit = LoadLibrary(L"Msftedit.dll")) == NULL) {
		if (IDYES != MessageBox(0, TranslateW(tszError), TranslateT("Information"), MB_YESNO | MB_ICONINFORMATION))
			return 1;
		return 0;
	}

	InitGlobals();
	RichUtil_Load();
	InitOptions();

	HookEvent(ME_DB_EVENT_ADDED, MessageEventAdded);
	HookEvent(ME_DB_CONTACT_SETTINGCHANGED, MessageSettingChanged);
	HookEvent(ME_DB_CONTACT_DELETED, ContactDeleted);
	HookEvent(ME_SYSTEM_MODULESLOADED, SplitmsgModulesLoaded);
	HookEvent(ME_SKIN_ICONSCHANGED, IconsChanged);
	HookEvent(ME_PROTO_CONTACTISTYPING, TypingMessage);
	HookEvent(ME_SYSTEM_PRESHUTDOWN, PreshutdownSendRecv);
	HookEvent(ME_CLIST_PREBUILDCONTACTMENU, PrebuildContactMenu);

	CreateServiceFunction(MS_MSG_SENDMESSAGE, SendMessageCommand);
	CreateServiceFunction(MS_MSG_SENDMESSAGEW, SendMessageCommand_W);
	CreateServiceFunction(MS_MSG_GETWINDOWAPI, GetWindowAPI);
	CreateServiceFunction(MS_MSG_GETWINDOWCLASS, GetWindowClass);
	CreateServiceFunction(MS_MSG_GETWINDOWDATA, GetWindowData);
	CreateServiceFunction(MS_MSG_SETSTATUSTEXT, SetStatusText);
	CreateServiceFunction("SRMsg/ReadMessage", ReadMessageCommand);

	hHookWinEvt = CreateHookableEvent(ME_MSG_WINDOWEVENT);
	hHookWinPopup = CreateHookableEvent(ME_MSG_WINDOWPOPUP);
	hHookWinWrite = CreateHookableEvent(ME_MSG_PRECREATEEVENT);

	SkinAddNewSoundEx("RecvMsgActive", LPGEN("Instant messages"), LPGEN("Incoming (focused window)"));
	SkinAddNewSoundEx("RecvMsgInactive", LPGEN("Instant messages"), LPGEN("Incoming (unfocused window)"));
	SkinAddNewSoundEx("AlertMsg", LPGEN("Instant messages"), LPGEN("Incoming (new session)"));
	SkinAddNewSoundEx("SendMsg", LPGEN("Instant messages"), LPGEN("Outgoing"));
	SkinAddNewSoundEx("SendError", LPGEN("Instant messages"), LPGEN("Message send error"));
	SkinAddNewSoundEx("TNStart", LPGEN("Instant messages"), LPGEN("Contact started typing"));
	SkinAddNewSoundEx("TNStop", LPGEN("Instant messages"), LPGEN("Contact stopped typing"));

	hCurSplitNS = LoadCursor(NULL, IDC_SIZENS);
	hCurSplitWE = LoadCursor(NULL, IDC_SIZEWE);
	hCurHyperlinkHand = LoadCursor(NULL, IDC_HAND);
	if (hCurHyperlinkHand == NULL)
		hCurHyperlinkHand = LoadCursor(g_hInst, MAKEINTRESOURCE(IDC_HYPERLINKHAND));

	InitStatusIcons();
	return 0;
}

void SplitmsgShutdown(void)
{
	DestroyCursor(hCurSplitNS);
	DestroyCursor(hCurHyperlinkHand);
	DestroyCursor(hCurSplitWE);

	DestroyHookableEvent(hHookWinEvt);
	DestroyHookableEvent(hHookWinPopup);
	DestroyHookableEvent(hHookWinWrite);

	FreeMsgLogIcons();
	FreeLibrary(hMsftEdit);
	RichUtil_Unload();
	msgQueue_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

CREOleCallback reOleCallback;

STDMETHODIMP CREOleCallback::QueryInterface(REFIID riid, LPVOID * ppvObj)
{
	if (IsEqualIID(riid, IID_IRichEditOleCallback)) {
		*ppvObj = this;
		AddRef();
		return S_OK;
	}
	*ppvObj = NULL;
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CREOleCallback::AddRef()
{
	if (refCount == 0)
		StgCreateDocfile(NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_DELETEONRELEASE, 0, &pictStg);

	return ++refCount;
}

STDMETHODIMP_(ULONG) CREOleCallback::Release()
{
	if (--refCount == 0) {
		if (pictStg) {
			pictStg->Release();
			pictStg = NULL;
		}
	}
	return refCount;
}

STDMETHODIMP CREOleCallback::ContextSensitiveHelp(BOOL)
{
	return S_OK;
}

STDMETHODIMP CREOleCallback::DeleteObject(LPOLEOBJECT)
{
	return S_OK;
}

STDMETHODIMP CREOleCallback::GetClipboardData(CHARRANGE*, DWORD, LPDATAOBJECT*)
{
	return E_NOTIMPL;
}

STDMETHODIMP CREOleCallback::GetContextMenu(WORD, LPOLEOBJECT, CHARRANGE*, HMENU*)
{
	return E_INVALIDARG;
}

STDMETHODIMP CREOleCallback::GetDragDropEffect(BOOL, DWORD, LPDWORD)
{
	return S_OK;
}

STDMETHODIMP CREOleCallback::GetInPlaceContext(LPOLEINPLACEFRAME*, LPOLEINPLACEUIWINDOW*, LPOLEINPLACEFRAMEINFO)
{
	return E_INVALIDARG;
}

STDMETHODIMP CREOleCallback::GetNewStorage(LPSTORAGE * lplpstg)
{
	WCHAR szwName[64];
	char szName[64];
	mir_snprintf(szName, "s%u", nextStgId++);
	MultiByteToWideChar(CP_ACP, 0, szName, -1, szwName, _countof(szwName));
	if (pictStg == NULL)
		return STG_E_MEDIUMFULL;
	return pictStg->CreateStorage(szwName, STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_CREATE, 0, 0, lplpstg);
}

STDMETHODIMP CREOleCallback::QueryAcceptData(LPDATAOBJECT, CLIPFORMAT*, DWORD, BOOL, HGLOBAL)
{
	return S_OK;
}

STDMETHODIMP CREOleCallback::QueryInsertObject(LPCLSID, LPSTORAGE, LONG)
{
	return S_OK;
}

STDMETHODIMP CREOleCallback::ShowContainerUI(BOOL)
{
	return S_OK;
}
