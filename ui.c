typedef VOID(*TEXTENTERFUNC)(char* msg, int len);

HWND window, messages, users, textEntry;
TEXTENTERFUNC _textEnterFunc;
int uiReady;
char uidLines[8];

void ui_add_message(char* msg, char* username, unsigned char nameR, unsigned char nameG, unsigned char nameB, unsigned char msgR, unsigned char msgG, unsigned char msgB) {
	static char prevUsername[32] = "";
	static ULONGLONG prevTick = 0;
	static CHARFORMAT fmt = { .cbSize = sizeof(CHARFORMAT),.dwMask = CFM_COLOR };
	ULONGLONG tick = GetTickCount64();
	SendMessage(messages, EM_SETSEL, 0, -1); //select all
	SendMessage(messages, EM_SETSEL, -1, 0); //deselect
	if (username && (tick - prevTick > 60000 || strcmp(username,prevUsername) != 0)) {
		SYSTEMTIME localTime;
		GetLocalTime(&localTime);
		char timeString[32];
		GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &localTime, " hh:mm\r\n", timeString, 32);
		fmt.crTextColor = RGB(nameR, nameG, nameB);
		SendMessage(messages, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
		SendMessage(messages, EM_REPLACESEL, 0, (LPARAM)username);
		fmt.crTextColor = RGB(100, 100, 100);
		SendMessage(messages, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
		SendMessage(messages, EM_REPLACESEL, 0, (LPARAM)timeString);
	}
	fmt.crTextColor = RGB(msgR, msgG, msgB);
	SendMessage(messages, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
	SendMessage(messages, EM_REPLACESEL, 0, (LPARAM)msg);
	SendMessage(messages, EM_REPLACESEL, 0, (LPARAM)"\r\n");
	SendMessage(messages, EM_SCROLLCARET, 0, 0);
	prevTick = tick;
	if(username)
		memcpy(prevUsername, username, 32);
}

void ui_edit_user_name(int uid, char* name, unsigned char r, unsigned char g, unsigned char b) {
	int lineIndex = uidLines[uid];
	if (lineIndex == -1) {
		lineIndex = (int)SendMessage(users, EM_GETLINECOUNT, 0, 0) -1;
		uidLines[uid] = lineIndex;
		SendMessage(users, EM_SETSEL, 0, -1); //select all
		SendMessage(users, EM_SETSEL, -1, 0); //deselect
		SendMessage(users, EM_REPLACESEL, 0, (LPARAM)"\n");
	}
	int i = (int)SendMessage(users, EM_LINEINDEX, (WPARAM)lineIndex, 0);
	int i2 = (int)SendMessage(users, EM_LINEINDEX, (WPARAM)(lineIndex+1), 0);
	if (name == 0) {
		SendMessage(users, EM_SETSEL, (WPARAM)i, (LPARAM)i2);
		SendMessage(users, EM_REPLACESEL, (WPARAM)0, (LPARAM)"");
		for (int i = 0; i < sizeof(uidLines); i++)
			if(uidLines[i] > lineIndex)
				uidLines[i]--;
		uidLines[uid] = -1;
		return;
	}
	static CHARFORMAT fmt = { .cbSize = sizeof(CHARFORMAT),.dwMask = CFM_COLOR };
	fmt.crTextColor = RGB(r, g, b);
	SendMessage(users, EM_SETSEL, i, i2 - 1);
	SendMessage(users, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&fmt);
	SendMessage(users, EM_REPLACESEL, 0, (LPARAM)name);
}

void ui_edit_user_status() {

}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg)
	{
	case WM_DESTROY: {
		PostQuitMessage(0);
		break;
	}
	case WM_SIZE: {
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);
		SetWindowPos(textEntry, HWND_TOP, 1, height-21, width-2, 20, 0);
		SetWindowPos(users, HWND_TOP, width-121, 0, 120, height-22, 0);
		SetWindowPos(messages, HWND_TOP, 1, 0, width-123, height-22, 0);
		break;
	}
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

WNDPROC textEntryOrigProc;
LRESULT CALLBACK textEntryProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
		char text[128];
		int res = GetWindowText(textEntry, text, 128);
		if (res > 0) {
			SetWindowText(textEntry, "");
			_textEnterFunc(text, res);
		}
	}
	return CallWindowProc(textEntryOrigProc, hwnd, uMsg, wParam, lParam);
}

void ui_thread() {
	HINSTANCE hInstance = GetModuleHandle(0);
	WNDCLASS wc = { 0 };
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = "Meow";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
	wc.hbrBackground = CreateSolidBrush(RGB(145, 70, 255));
	RegisterClass(&wc);
	window = CreateWindow("Meow", "Meow", WS_BORDER | WS_SYSMENU | WS_VISIBLE | WS_MINIMIZEBOX | WS_SIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 539, 317, NULL, NULL, hInstance, NULL);
	if (window == NULL)
		fatalError("failed to create window");

	LoadLibrary("riched32.dll");
	messages = CreateWindow("RichEdit", "", WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_DISABLENOSCROLL, 1, 0, 400, 256, window, NULL, hInstance, NULL);
	SendMessage(messages, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));

	CHARFORMAT fmt = { 0 };
	fmt.cbSize = sizeof(CHARFORMAT);
	fmt.dwMask = CFM_COLOR | CFM_FACE; //CFM_SIZE
	fmt.crTextColor = RGB(200, 200, 200);
	str_cat(fmt.szFaceName, "Consolas");
	//fmt.yHeight = 220;
	SendMessage(messages, EM_SETCHARFORMAT, 0, (LPARAM)&fmt); //set default font

	textEntry = CreateWindow("RichEdit", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_WANTRETURN, 1, 257, 521, 20, window, NULL, hInstance, NULL);
	SendMessage(textEntry, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0, 0, 0));
	SendMessage(textEntry, EM_SETCHARFORMAT, 0, (LPARAM)&fmt);
	SendMessage(textEntry, EM_LIMITTEXT, 127, 0);
	textEntryOrigProc = (WNDPROC)SetWindowLongPtr(textEntry, GWLP_WNDPROC, (LONG_PTR)textEntryProc);

	users = CreateWindow("RichEdit", "", WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE, 402, 0, 120, 256, window, NULL, hInstance, NULL);
	SendMessage(users, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0, 0, 0));
	SendMessage(users, EM_SETCHARFORMAT, 0, (LPARAM)&fmt);

	uiReady = 1;

	MSG msg = { 0 };
	while (GetMessage(&msg, NULL, 0, 0) != 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	ExitProcess(0);
}

void ui_init(TEXTENTERFUNC textEnteredCallback) {
	_textEnterFunc = textEnteredCallback;
	for (int i = 0; i < sizeof(uidLines); i++)
		uidLines[i] = -1;
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ui_thread, NULL, 0, NULL);
	while (!uiReady)
		Sleep(1);
}

