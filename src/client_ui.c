#define MAX_MESSAGES 64
#define MAX_MESSAGE_LEN 256
#define MAX_WINDOW_SIZE 256

struct meow_ui_context {
    unsigned char messages[MAX_MESSAGES * MAX_MESSAGE_LEN], messageDraft[MAX_MESSAGE_LEN], peerList[MAX_WINDOW_SIZE * (2 + 2 + 16)];
    int messageIndex, messageDraftLen, chatLine, lastMessageUid, peerCount;
    short width, height;
    HANDLE hStdOut, hStdIn;
    CHAR_INFO screenBuffer[MAX_WINDOW_SIZE * MAX_WINDOW_SIZE];
    SYSTEMTIME lastMessageTime;
    void	(*sendChatMessageFunc)(char* msg, int len);
}g_ui;

void meow_ui_update_console_region(short x, short y, short w, short h) {
    SMALL_RECT writeRegion = { x,y, x + w - 1, y + h - 1 };
    WriteConsoleOutputA(g_ui.hStdOut, g_ui.screenBuffer, (COORD) { g_ui.width, g_ui.height }, (COORD) { x, y }, & writeRegion);
}

void meow_ui_render_message(unsigned char* msg) {
    int linePos = 0, msgPos = 0, attrib = 7;
    for (; msg[msgPos]; msgPos += msg[msgPos] && msg[msgPos] < 16) {
        for (int i = 0; g_ui.chatLine > g_ui.height - 4 && i < g_ui.height - 4; i++) memcpy(g_ui.screenBuffer + i * g_ui.width, g_ui.screenBuffer + (i + 1) * g_ui.width, (g_ui.width - 17) * sizeof(CHAR_INFO));
        memset(g_ui.screenBuffer + g_ui.width * min(g_ui.height - 4, g_ui.chatLine), 0, (g_ui.width - 17) * sizeof(CHAR_INFO));
        for (; msg[msgPos]; msgPos++) {
            if (msg[msgPos] == '\n' || linePos >= g_ui.width - 17) break;
            if (msg[msgPos] < 16) { attrib = msg[msgPos]; continue; }
            *(unsigned int*)&g_ui.screenBuffer[g_ui.width * min(g_ui.height - 4, g_ui.chatLine) + linePos] = msg[msgPos] + (attrib << 16);
            linePos++;
        }
        g_ui.chatLine++, linePos = 0;
    }
}

void meow_ui_new_message(char* msg, char* userName, int userId) {
    SYSTEMTIME localTime;
    GetLocalTime(&localTime);
    sprintf_s((char*)(g_ui.messages + g_ui.messageIndex * MAX_MESSAGE_LEN), MAX_MESSAGE_LEN, "\13%s\10 %.2i:%.2i\n\7", userName, localTime.wHour, localTime.wMinute);
    int headerLen = *userName && ((g_ui.lastMessageTime.wMinute != localTime.wMinute) || (g_ui.lastMessageUid != userId)) ? (int)strlen((char*)(g_ui.messages + g_ui.messageIndex * MAX_MESSAGE_LEN)) : 0;
    int msgLen = min(headerLen + (int)strlen(msg), MAX_MESSAGE_LEN - 1);
    memcpy(g_ui.messages + g_ui.messageIndex * MAX_MESSAGE_LEN + headerLen, msg, msgLen - headerLen);
    g_ui.messages[g_ui.messageIndex * MAX_MESSAGE_LEN + msgLen] = 0;
    meow_ui_render_message(g_ui.messages + g_ui.messageIndex * MAX_MESSAGE_LEN);
    meow_ui_update_console_region(0, 0, g_ui.width - 17, g_ui.height - 3);
    g_ui.messageIndex = (g_ui.messageIndex + 1) % MAX_MESSAGES;
    g_ui.lastMessageTime = localTime;
    g_ui.lastMessageUid = userId;
}

void meow_ui_update_draft() {
    for (int i = 0; i < g_ui.width * 2 - 1; i++) g_ui.screenBuffer[g_ui.width * (g_ui.height - 2) + 2 + i].Char.AsciiChar = (i < g_ui.messageDraftLen) ? g_ui.messageDraft[i] : 0;
    SetConsoleCursorPosition(g_ui.hStdOut, (COORD) { 2 + g_ui.messageDraftLen % (g_ui.width - 2), g_ui.height - 2 + (g_ui.messageDraftLen > g_ui.width - 3) });
    meow_ui_update_console_region(0, g_ui.height - 2, g_ui.width, 2);
}

void meow_ui_update_peerlist(short userId, char* userName, unsigned short color) {
    int listIndex = 0;
    for (; listIndex < MAX_WINDOW_SIZE - 1 && listIndex < g_ui.peerCount && *(short*)(g_ui.peerList + listIndex * 20) != userId; listIndex++);
    g_ui.peerCount += (listIndex == g_ui.peerCount) - (!userName);
    *(int*)(g_ui.peerList + listIndex * 20) = userId + (color << 16);
    memset(g_ui.peerList + listIndex * 20 + 4, 0, 16);
    if (userName) memcpy(g_ui.peerList + listIndex * 20 + 4, userName, strlen(userName) + 1);
    else memcpy(g_ui.peerList + listIndex * 20, g_ui.peerList + (listIndex + 1) * 20, (MAX_WINDOW_SIZE - (listIndex + 1)) * 20);
    for (listIndex *= (userName != 0); listIndex <= g_ui.peerCount && listIndex < g_ui.height - 3 && (!userName || *(short*)(g_ui.peerList + listIndex * 20) == userId); listIndex++)
        for (int i = 0; i < 16; i++) *(unsigned int*)&g_ui.screenBuffer[g_ui.width * listIndex + (g_ui.width - 16 + i)] = (g_ui.peerList[listIndex * 20 + 4 + i] + (*(short*)(g_ui.peerList + listIndex * 20 + 2) << 16)) * (listIndex < g_ui.peerCount);
    meow_ui_update_console_region(g_ui.width - 16, userName ? (short)listIndex - 1 : 0, 16, userName ? 1 : g_ui.height - 3);
}

void meow_ui_resize() {
    memset(g_ui.screenBuffer, 0, g_ui.width * g_ui.height * sizeof(CHAR_INFO));
    for (int i = 0; i < g_ui.width * g_ui.height; i++) g_ui.screenBuffer[i].Attributes = 7;
    for (int i = 0; i < g_ui.width; i++) g_ui.screenBuffer[g_ui.width * (g_ui.height - 3) + i].Char.AsciiChar = '\304';
    for (int i = 0; i < g_ui.height - 3; i++) g_ui.screenBuffer[g_ui.width - 17 + (i * g_ui.width)].Char.AsciiChar = '\263';
    g_ui.screenBuffer[g_ui.width * (g_ui.height - 3) + g_ui.width - 17].Char.AsciiChar = '\301';
    g_ui.screenBuffer[g_ui.width * (g_ui.height - 2)].Char.AsciiChar = '>';
    g_ui.chatLine = 0;
    for (int i = 0; i < MAX_MESSAGES; i++) meow_ui_render_message(g_ui.messages + (g_ui.messageIndex + i) % MAX_MESSAGES * MAX_MESSAGE_LEN);
    meow_ui_update_draft();
    meow_ui_update_peerlist(~0, 0, 0);
    meow_ui_update_console_region(0, 0, g_ui.width, g_ui.height);
}

void meow_ui_init() {
    if (g_ui.hStdOut == NULL) {
        g_ui.hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        g_ui.hStdIn = GetStdHandle(STD_INPUT_HANDLE);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)meow_ui_init, NULL, 0, NULL);
        for (volatile short* val = &g_ui.width;;) if (*val) return;
    }
    SetConsoleCP(850);
    SetConsoleOutputCP(850);
    SetConsoleMode(g_ui.hStdIn, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    INPUT_RECORD records[128];
    DWORD numEventsRead = 0;
    records[0].EventType = WINDOW_BUFFER_SIZE_EVENT;
    WriteConsoleInput(g_ui.hStdIn, records, 1, &numEventsRead);
    while (1) {
        ReadConsoleInputA(g_ui.hStdIn, records, 128, &numEventsRead);
        for (unsigned int i = 0; i < numEventsRead; i++) {
            if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
                GetConsoleScreenBufferInfo(g_ui.hStdOut, &consoleInfo);
                g_ui.width = min(max(consoleInfo.dwSize.X, 20), MAX_WINDOW_SIZE);
                g_ui.height = min(max(consoleInfo.srWindow.Bottom - consoleInfo.srWindow.Top + 1, 10), MAX_WINDOW_SIZE);
                COORD test = { max(consoleInfo.dwSize.X, 20), max(consoleInfo.srWindow.Bottom - consoleInfo.srWindow.Top + 1, 10) };
                if (SetConsoleScreenBufferSize(g_ui.hStdOut, test) == 0) return;
                meow_ui_resize();
            }
            else if (records[i].EventType == KEY_EVENT && records[i].Event.KeyEvent.bKeyDown && (g_ui.messageDraft[g_ui.messageDraftLen] = records[i].Event.KeyEvent.uChar.AsciiChar) != 0) {
                if (g_ui.messageDraft[g_ui.messageDraftLen] == '\r' && (g_ui.messageDraft[g_ui.messageDraftLen] = 0) == 0 && g_ui.messageDraftLen > 0) g_ui.sendChatMessageFunc((char*)g_ui.messageDraft, g_ui.messageDraftLen);
                g_ui.messageDraftLen = min(max((g_ui.messageDraftLen + ((g_ui.messageDraft[g_ui.messageDraftLen] == '\b') ? -1 : 1)) * (g_ui.messageDraft[g_ui.messageDraftLen]!=0), 0), MAX_MESSAGE_LEN);
                meow_ui_update_draft();
            }
        }
    }
}