#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <ShlObj.h>
#include <Richedit.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <initguid.h>
#include <mmreg.h>
#include <dsound.h>

void fatalError(char* msg) {
	MessageBoxA(NULL, msg, "fatal error", 0);
	ExitProcess(0);
}

#include "stringstuff.c"
#include "ui.c"
#include "aes_256.c"
#include "iniparser.c"

#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "user32")
#pragma comment (lib, "shell32")
#pragma comment (lib, "gdi32")
#pragma comment (lib, "dsound.lib")
#pragma comment (lib, "ws2_32.lib")

#define MAX_PEERS 8
#define MAX_DROPPED_PACKETS 6

#define ISVALID_NUMCHANNELS(x) (x==1||x==2)
#define ISVALID_BITDEPTH(x) (x==8||x==16||x==24)
#define ISVALID_SAMPLERATE(x) (x==8000||x==11025||x==16000||x==22050||x==32000||x==44100)

#define CEIL16(x) ((x-1)+(16-(x-1)%16))

#define PACKET_TYPE_INFO    0
#define PACKET_TYPE_CHAT    1
#define PACKET_TYPE_AUDIO   2
#define PACKET_TYPE_RESEND  3
#define PACKET_TYPE_KEEPALIVE 4

struct peer {
	char            nickname[16];
	unsigned int    numChannels;
	unsigned int    bitDepth;
	unsigned int    sampleRate;
	float           volume;
	SOCKET          socket;
	unsigned int    ip;
}peers[MAX_PEERS];

struct meow_config {
	unsigned int    numChannels;
	unsigned int    bitDepth;
	unsigned int    sampleRate;
	unsigned int    toggleKey;
	unsigned int    holdKey;
	char            nickname[16];
	unsigned char   key[32];
    unsigned char   id[8];
	char            address[32];
	char            port[8];
	unsigned int    targetDelay;
	unsigned int    transmitInterval;
	unsigned int    noiseGateThreshold;
}cfg;

unsigned char   keyScheduleEnc[240], keyScheduleDec[240], initVec[16];
LPDIRECTSOUND8  directSoundInterface;
unsigned char   sentPackets[MAX_DROPPED_PACKETS * 65536];
unsigned short  sentPacketLengths[MAX_DROPPED_PACKETS];
unsigned int    sentPacketIndex = 0;
unsigned int    sentPacketSeqNum = 0;
int             debug_droptest = 0;
char			configPath[MAX_PATH];


void Callback_SendChatMessage(char* msg, int msgLen) {
	//process commands
	if (*msg == '/') {
		int     argc = 0;
		char* argv[3] = { 0 };
		for (int i = 0; i < msgLen; i++) {
			if (msg[i] == ' ') {
				argv[argc] = msg + i + 1;
				argc++;
			}
		}
		//command: /vol [string peername] [float volume]
		if (*(int*)msg == *(int*)"/vol" && argc == 2) {
			for (unsigned int i = 0; i < MAX_PEERS; i++) {
				if (peers[i].socket != INVALID_SOCKET && memcmp(peers[i].nickname, argv[0], min(strlen(peers[i].nickname), (size_t)(argv[1] - argv[0] - 1))) == 0) {
                    peers[i].volume = str_getfloat(argv[1]);
					return;
				}
			}
		}
		//command: /ng [int threshold]
		if (*(int*)msg == *(int*)"/ng " && argc > 0) {
			cfg.noiseGateThreshold = str_getint(argv[0]);
			return;
		}
		//command: /drop [int count]
		if (*(int*)msg == *(int*)"/dro") {
			debug_droptest = 1;
			if (argc > 0) debug_droptest = str_getint(argv[0]);
			return;
		}
		//command: /cfg
		if (*(int*)msg == *(int*)"/cfg") {
			ShellExecuteA(NULL, "open", configPath, NULL, NULL, SW_SHOW);
			return;
		}
        //command: /help
        if (*(int*)msg == *(int*)"/hel") {
            ui_add_message(
                ""
                "/vol [string peername] [float volume]\r\n"
                "/ng [int threshold]\r\n"
                "/drop [int count]\r\n"
                "/cfg\r\n"
                ""
                , 0, 0, 0, 0, 100, 100, 100);
            return;
        }
	}
	//display & send message
	ui_add_message(msg, cfg.nickname, 100, 200, 200, 200, 200, 200);
	unsigned char* sendBuf = sentPackets + sentPacketIndex * 65536;
	sendBuf[0] = PACKET_TYPE_CHAT;
	*((unsigned int*)(sendBuf + 1)) = sentPacketSeqNum;
	memcpy(sendBuf + 5, msg, msgLen + 1); //+1 to include null
	int sendLen = CEIL16(5 + msgLen + 1);
	AES_256_CBC_encrypt(sendBuf, sendLen, initVec, keyScheduleEnc);
	sentPacketLengths[sentPacketIndex] = (unsigned short)sendLen;
	sentPacketSeqNum++;
	sentPacketIndex = (sentPacketIndex + 1) % MAX_DROPPED_PACKETS;
	if (debug_droptest) {
		debug_droptest--;
		return;
	}
	for (int i = 0; i < MAX_PEERS; i++) {
		if (peers[i].socket != INVALID_SOCKET) {
			if (send(peers[i].socket, (char*)sendBuf, sendLen, 0) == SOCKET_ERROR) {
				ui_add_message("Error: send()", 0, 0, 0, 0, 200, 0, 0);
			}
		}
	}
}

int abs(int n) {
    if (n < 0)
        n *= -1;
    return n;
}

void AudioCaptureThread() {
    //create IDirectSoundCapture8 interface
    LPDIRECTSOUNDCAPTURE8 capture = NULL;
    HRESULT res = DirectSoundCaptureCreate8(NULL, &capture, NULL);
    if (res != DS_OK) {
        ui_add_message("Error: DirectSoundCaptureCreate8", 0, 0, 0, 0, 200, 0, 0);
        return;
}
    //create IDirectSoundCaptureBuffer
    WAVEFORMATEX waveformat = { 0 };
    waveformat.wFormatTag = WAVE_FORMAT_PCM;
    waveformat.nChannels = (WORD)cfg.numChannels;
    waveformat.nSamplesPerSec = cfg.sampleRate;
    waveformat.wBitsPerSample = (WORD)cfg.bitDepth;
    waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
    waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
    DSCBUFFERDESC bufferdesc = { 0 };
    bufferdesc.dwSize = sizeof(DSCBUFFERDESC);
    bufferdesc.dwBufferBytes = waveformat.nAvgBytesPerSec; //1 second buffer
    bufferdesc.lpwfxFormat = &waveformat;
    LPDIRECTSOUNDCAPTUREBUFFER tmpbuffer = NULL;
    res = capture->lpVtbl->CreateCaptureBuffer(capture, &bufferdesc, &tmpbuffer, NULL);
    if (res != DS_OK) {
        ui_add_message("Error: CreateCaptureBuffer", 0, 0, 0, 0, 200, 0, 0);
        return;
    }
    //get IDirectSoundCaptureBuffer8
    LPDIRECTSOUNDCAPTUREBUFFER8 captureBuffer = NULL;
    res = tmpbuffer->lpVtbl->QueryInterface(tmpbuffer, &IID_IDirectSoundCaptureBuffer8, (LPVOID*)&captureBuffer);
    if (res != DS_OK) {
        ui_add_message("Error: QueryInterface", 0, 0, 0, 0, 200, 0, 0);
        return;
    }
    tmpbuffer->lpVtbl->Release(tmpbuffer);
    //capture loop variables
    DWORD	cbPos = 0;
    DWORD	cbLastPos = 0;
    LPVOID	cbPtr1 = NULL;
    DWORD	cbLen1 = 0;
    LPVOID	cbPtr2 = NULL;
    DWORD	cbLen2 = 0;
    DWORD	cbBytesToLock = 0;
    int     recording = 0;
    int     pressedKeys = 0;
    float   noiseGateVolume = 0;
    int     notifyStop = 0;
    int     transmit = 1;
    int     silenceCounter = 0;
    //capture loop
    while (1) {
        //toggle recording based on keyboard input
        int pressingKeys = ((GetAsyncKeyState(cfg.toggleKey) != 0) << 1) + (GetAsyncKeyState(cfg.holdKey) != 0);
        if ((!pressedKeys && pressingKeys) + (pressedKeys + pressingKeys == 1)) {
            recording = !recording;
            if (recording) {
                ui_edit_user_name(0, cfg.nickname, 0, 100, 200);
                res = captureBuffer->lpVtbl->Start(captureBuffer, DSCBSTART_LOOPING);
                if (res != DS_OK) {
                    ui_add_message("Error: capture start", 0, 0, 0, 0, 200, 0, 0);
                    return;
                }
            }
            else {
                ui_edit_user_name(0, cfg.nickname, 100, 200, 200);
                notifyStop = 1;
            }
        }
        pressedKeys = pressingKeys;
        //
        if (recording || notifyStop) {
            if (captureBuffer->lpVtbl->GetCurrentPosition(captureBuffer, NULL, &cbPos) != DS_OK) {
                ui_add_message("Error: capture getcurrentposition", 0, 0, 0, 0, 200, 0, 0);
                continue;
            }
            cbBytesToLock = (cbPos >= cbLastPos) ? (cbPos - cbLastPos) : (bufferdesc.dwBufferBytes - cbLastPos + cbPos);
            if (cbBytesToLock == 0) {
                Sleep(cfg.transmitInterval);
                continue;
            }
            if (captureBuffer->lpVtbl->Lock(captureBuffer, cbLastPos, cbBytesToLock, &cbPtr1, &cbLen1, &cbPtr2, &cbLen2, 0) != DS_OK) {
                ui_add_message("Error: capture lock", 0, 0, 0, 0, 200, 0, 0);
                continue;
            }
            cbLastPos = cbPos;
            if (cbLen1 + cbLen2 > 65000) {
                ui_add_message("Error: captured too much data", 0, 0, 0, 0, 200, 0, 0);
                continue;
            }
            //build packet
            unsigned char* sendBuf = sentPackets + sentPacketIndex * 65536;
            sendBuf[0] = PACKET_TYPE_AUDIO;
            *((unsigned int*)(sendBuf + 1)) = sentPacketSeqNum;
            *((unsigned short*)(sendBuf + 5)) = (unsigned short)(cbLen1 + cbLen2);
            memcpy(sendBuf + 7, cbPtr1, cbLen1);
            memcpy(sendBuf + 7 + cbLen1, cbPtr2, cbLen2);
            int sendLen = CEIL16(7 + cbLen1 + cbLen2);
            if (captureBuffer->lpVtbl->Unlock(captureBuffer, cbPtr1, cbLen1, cbPtr2, cbLen2) != DS_OK) {
                ui_add_message("Error: capture unlock", 0, 0, 0, 0, 200, 0, 0);
                return;
            }
            //audio processing
            if (cfg.noiseGateThreshold != 0 && cfg.bitDepth == 16) {
                for (short* sample = (short*)(sendBuf + 7); sample < (short*)(sendBuf + sendLen); sample++) {
                    noiseGateVolume += -0.0002f + ((unsigned int)abs(*sample) > cfg.noiseGateThreshold) * 0.0005f;
                    noiseGateVolume = min(1.0f, max(0.0f, noiseGateVolume));
                    *sample = (short)(*sample * noiseGateVolume);
                }
                silenceCounter = (noiseGateVolume <= 0.01) ? silenceCounter + 1 : 0;
                notifyStop |= silenceCounter >= (int)(cfg.targetDelay / cfg.transmitInterval);
                transmit = (transmit || !silenceCounter) && !notifyStop;
            }
            //
            if (notifyStop) {
                if (!recording && (captureBuffer->lpVtbl->Stop(captureBuffer) != DS_OK)) {
                    ui_add_message("Error: capture stop", 0, 0, 0, 0, 200, 0, 0);
                    return;
                }
                *((unsigned short*)(sendBuf + 5)) = 0;
                sendLen = 16;
            }
            if (transmit || notifyStop) {
                notifyStop = 0;
                AES_256_CBC_encrypt(sendBuf, sendLen, initVec, keyScheduleEnc);
                sentPacketLengths[sentPacketIndex] = (unsigned short)sendLen;
                sentPacketSeqNum++;
                sentPacketIndex = (sentPacketIndex + 1) % MAX_DROPPED_PACKETS;
                if (debug_droptest) {
                    debug_droptest--;
                    continue;
                }
                for (int i = 0; i < MAX_PEERS; i++) {
                    if (peers[i].socket != INVALID_SOCKET) {
                        if (send(peers[i].socket, (char*)sendBuf, sendLen, 0) == SOCKET_ERROR) {
                            ui_add_message("Error: capture send", 0, 0, 0, 0, 200, 0, 0);
                            return;
                        }
                    }
                }
            }
        }
        //
        Sleep(cfg.transmitInterval);
    }
}

void PeerThread(LPVOID lParam) {
    int peerIndex = *(int*)lParam;
    struct peer* pPeer = &peers[peerIndex];
    pPeer->numChannels = 0;
    pPeer->volume = 1.0f;
    LPDIRECTSOUNDBUFFER8 soundBuffer = NULL;
    DWORD	sbTotalLen = 0;
    DWORD	sbPos = 0;
    DWORD	sbLastPos = 0;
    LPVOID	sbPtr1 = 0;
    DWORD	sbLen1 = 0;
    LPVOID	sbPtr2 = 0;
    DWORD	sbLen2 = 0;
    int		sbPlaying = 0;
    int		sbDelay = 0;
    int     sbTargetDelay = 0;
    int     triggerAudioPause = 0;
    unsigned char* recvBuf = malloc(MAX_DROPPED_PACKETS * 65536);
    unsigned int   recvBufCount = 0;
    unsigned int   recvBufIndex = 0;
    int     recvLen = 0;
    int     recvTimeoutCount = 0;
    ULONGLONG  keepAliveTime = GetTickCount64();
    unsigned int expectedSeqNum = 0;
    //exchange peer info / initialize peer
    unsigned char clientInfo[48];
    clientInfo[0] = PACKET_TYPE_INFO;
    *((unsigned int*)(clientInfo + 1)) = sentPacketSeqNum;
    memcpy(clientInfo + 5, cfg.nickname, 16);
    ((unsigned int*)(clientInfo + 5 + 16))[0] = cfg.numChannels;
    ((unsigned int*)(clientInfo + 5 + 16))[1] = cfg.bitDepth;
    ((unsigned int*)(clientInfo + 5 + 16))[2] = cfg.sampleRate;
    AES_256_CBC_encrypt(clientInfo, sizeof(clientInfo), initVec, keyScheduleEnc);
    int success = recvBuf != 0 && (send(pPeer->socket, (char*)clientInfo, sizeof(clientInfo), 0) != SOCKET_ERROR) && (recv(pPeer->socket, (char*)recvBuf, 65536, 0) == sizeof(clientInfo)) && AES_256_CBC_decrypt(recvBuf, sizeof(clientInfo), initVec, keyScheduleDec) && memcpy(pPeer, recvBuf + 5, 28) && !recvBuf[0] && ISVALID_NUMCHANNELS(pPeer->numChannels) && ISVALID_BITDEPTH(pPeer->bitDepth) && ISVALID_SAMPLERATE(pPeer->sampleRate);
    if (!success)
        ui_add_message("Error: Failed to communicate with peer", 0, 0, 0, 0, 200, 0, 0);
    while (success) {
        success = 0;
        expectedSeqNum = *((unsigned int*)(recvBuf + 1));
        //resend our info in case the peer didnt receive the first one
        if (send(pPeer->socket, (char*)clientInfo, sizeof(clientInfo), 0) == SOCKET_ERROR) {
            ui_add_message("Error: peer send", 0, 0, 0, 0, 200, 0, 0);
            break;
        }
        //sanitize nickname
        int i;
        for (i = 0; i < 16 && pPeer->nickname[i] >= 32 && pPeer->nickname[i] <= 126; i++);
        pPeer->nickname[i] = 0;
        if (i == 0) memcpy(pPeer->nickname, "Anonymous", 10);
        ui_edit_user_name((short)peerIndex + 1, pPeer->nickname, 100, 200, 200);
        //create IDirectSoundBuffer
        WAVEFORMATEX waveformat = { 0 };
        waveformat.wFormatTag = WAVE_FORMAT_PCM;
        waveformat.nChannels = (WORD)pPeer->numChannels;
        waveformat.nSamplesPerSec = pPeer->sampleRate;
        waveformat.wBitsPerSample = (WORD)pPeer->bitDepth;
        waveformat.nBlockAlign = waveformat.nChannels * waveformat.wBitsPerSample / 8;
        waveformat.nAvgBytesPerSec = waveformat.nSamplesPerSec * waveformat.nBlockAlign;
        DSBUFFERDESC bufferdesc = { 0 };
        bufferdesc.dwSize = sizeof(DSBUFFERDESC);
        bufferdesc.dwFlags = DSBCAPS_GLOBALFOCUS;
        bufferdesc.dwBufferBytes = waveformat.nAvgBytesPerSec; //1 second buffer
        bufferdesc.lpwfxFormat = &waveformat;
        LPDIRECTSOUNDBUFFER tmpbuffer = NULL;
        if (directSoundInterface->lpVtbl->CreateSoundBuffer(directSoundInterface, &bufferdesc, &tmpbuffer, NULL) != DS_OK) {
            ui_add_message("Error: peer createsoundbuffer", 0, 0, 0, 0, 200, 0, 0);
            break;
        }
        //get IDirectSoundBuffer8
        if (tmpbuffer->lpVtbl->QueryInterface(tmpbuffer, &IID_IDirectSoundBuffer8, (LPVOID*)&soundBuffer) != DS_OK) {
            ui_add_message("Error: peer QueryInterface", 0, 0, 0, 0, 200, 0, 0);
            break;
        }
        tmpbuffer->lpVtbl->Release(tmpbuffer);
        sbTargetDelay = waveformat.nAvgBytesPerSec / (1000 / cfg.targetDelay);
        sbTotalLen = bufferdesc.dwBufferBytes;
        success = 1;
        break;
    }
    //main receive loop
    while (success) {
        //send keep-alive if it's been more than a second from the previous one
        ULONGLONG curTime = GetTickCount64();
        if (curTime > keepAliveTime + 1000) {
            keepAliveTime = curTime;
            unsigned char tmp[16];
            tmp[0] = PACKET_TYPE_KEEPALIVE;
            *((unsigned int*)(tmp + 1)) = sentPacketSeqNum;
            AES_256_CBC_encrypt(tmp, 16, initVec, keyScheduleEnc);
            if (send(pPeer->socket, (char*)tmp, 16, 0) == SOCKET_ERROR) {
                ui_add_message("Error: peer send", 0, 0, 0, 0, 200, 0, 0);
                break;
            }
        }
        //receive
        unsigned char* pReceivedPacket = recvBuf + ((recvBufIndex + recvBufCount) % MAX_DROPPED_PACKETS) * 65536;
        recvLen = recv(pPeer->socket, (char*)pReceivedPacket, 65536, 0);
        if (recvLen == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                if (recvTimeoutCount > 0) {
                    ui_add_message("Peer timed out", 0, 0, 0, 0, 100, 100, 100);
                    break;
                }
                recvTimeoutCount++;
                continue;
            }
            ui_add_message("Error: peer recv", 0, 0, 0, 0, 200, 0, 0);
            break;
        }
        recvTimeoutCount = 0;
        if (recvLen % 16) continue;
        AES_256_CBC_decrypt(pReceivedPacket, recvLen, initVec, keyScheduleDec);
        unsigned int receivedSeqNum = *(unsigned int*)(pReceivedPacket + 1);
        if (receivedSeqNum < expectedSeqNum) continue;
        //store packet if its seqnum is higher than expected and higher than the previous stored seqnum (if there are previous stored packets in the queue)
        recvBufCount += (receivedSeqNum > expectedSeqNum) && (recvBufCount == 0 || receivedSeqNum > *(unsigned int*)(recvBuf + ((recvBufIndex + recvBufCount - 1) % MAX_DROPPED_PACKETS) * 65536 + 1));
        if (((receivedSeqNum - expectedSeqNum) >= MAX_DROPPED_PACKETS) || (recvBufCount >= MAX_DROPPED_PACKETS)) {
            ui_add_message("Dropped too many packets, skipping ahead...", 0, 0, 0, 0, 100, 100, 100);
            expectedSeqNum = receivedSeqNum;
            recvBufCount = 0;
            triggerAudioPause = sbPlaying;
        }
        //first process current packet if its seqnum is expected, then process the backlog of stored packets
        for (int first = receivedSeqNum == expectedSeqNum; (recvBufCount + first) > 0; first = 0) {
            if (!first) pReceivedPacket = recvBuf + recvBufIndex * 65536;
            if (*(unsigned int*)(pReceivedPacket + 1) != expectedSeqNum) {
                //unexpected seqnum, request expected sn
                unsigned char tmp[16];
                tmp[0] = PACKET_TYPE_RESEND;
                ((unsigned int*)(tmp + 1))[0] = sentPacketSeqNum;
                ((unsigned int*)(tmp + 1))[1] = expectedSeqNum;
                AES_256_CBC_encrypt(tmp, 16, initVec, keyScheduleEnc);
                send(pPeer->socket, (char*)tmp, 16, 0);
                break;
            }
            expectedSeqNum += pReceivedPacket[0] == PACKET_TYPE_CHAT || pReceivedPacket[0] == PACKET_TYPE_AUDIO;
            recvBufIndex = (recvBufIndex + !first) % MAX_DROPPED_PACKETS;
            recvBufCount -= !first;
            //process packet
            if (pReceivedPacket[0] == PACKET_TYPE_CHAT) {
                int i;
                for (i = 1; i < 256 && pReceivedPacket[5 + i] > 15; i++);
                pReceivedPacket[5 + i] = 0;
                if (i > 0)
                    ui_add_message((char*)pReceivedPacket + 5, pPeer->nickname, 100, 200, 200, 200, 200, 200);
            }
            else if (pReceivedPacket[0] == PACKET_TYPE_AUDIO) {
                unsigned int recvAudioLen = *((unsigned short*)(pReceivedPacket + 5));
                if (recvAudioLen > sbTotalLen / 2) {
                    ui_add_message("Error: peer audio recvlen >500ms", 0, 0, 0, 0, 200, 0, 0);
                    continue;
                }
                //audio processing
                if (pPeer->volume != 1.0f && pPeer->bitDepth == 16) {
                    for (short* sample = (short*)(pReceivedPacket + 7); sample < (short*)(pReceivedPacket + 7 + recvAudioLen); sample++)
                        *sample = (short)(*sample * pPeer->volume);
                }
                //add the samples
                if (recvAudioLen > 0) {
                    if (soundBuffer->lpVtbl->Lock(soundBuffer, sbLastPos, recvAudioLen, &sbPtr1, &sbLen1, &sbPtr2, &sbLen2, 0) != DS_OK) {
                        ui_add_message("Error: peer lock", 0, 0, 0, 0, 200, 0, 0);
                        success = 0;
                        break;
                    }
                    memcpy(sbPtr1, pReceivedPacket + 7, sbLen1);
                    memcpy(sbPtr2, pReceivedPacket + 7 + sbLen1, sbLen2);
                    if (soundBuffer->lpVtbl->Unlock(soundBuffer, sbPtr1, sbLen1, sbPtr2, sbLen2) != DS_OK) {
                        ui_add_message("Error: peer unlock", 0, 0, 0, 0, 200, 0, 0);
                        success = 0;
                        break;
                    }
                    sbLastPos = sbLastPos + recvAudioLen;
                    if (sbLastPos >= sbTotalLen) {
                        sbLastPos = sbLastPos - sbTotalLen;
                    }
                }
                //manage playback
                if (soundBuffer->lpVtbl->GetCurrentPosition(soundBuffer, &sbPos, NULL) != DS_OK) {
                    ui_add_message("Error: peer getcurrentposition", 0, 0, 0, 0, 200, 0, 0);
                    success = 0;
                    break;
                }
                if (sbLastPos >= sbPos) sbDelay = sbLastPos - sbPos;
                else sbDelay = sbTotalLen - sbPos + sbLastPos;
                if (sbPlaying && (recvAudioLen == 0 || triggerAudioPause)) {
                    triggerAudioPause = 0;
                    if (soundBuffer->lpVtbl->Stop(soundBuffer) != DS_OK) {
                        ui_add_message("Error: peer stop", 0, 0, 0, 0, 200, 0, 0);
                        success = 0;
                        break;
                    }
                    if (soundBuffer->lpVtbl->SetCurrentPosition(soundBuffer, sbLastPos) != DS_OK) {
                        ui_add_message("Error: peer setcurrentposition", 0, 0, 0, 0, 200, 0, 0);
                        success = 0;
                        break;
                    }
                    sbPlaying = 0;
                    sbDelay = 0;
                    ui_edit_user_name((short)peerIndex + 1, pPeer->nickname, 100, 200, 200);
                }
                if (!sbPlaying && sbDelay >= sbTargetDelay) {
                    if (soundBuffer->lpVtbl->Play(soundBuffer, 0, 0, DSBPLAY_LOOPING) != DS_OK) {
                        ui_add_message("Error: peer play", 0, 0, 0, 0, 200, 0, 0);
                        success = 0;
                        break;
                    }
                    sbPlaying = 1;
                    ui_edit_user_name((short)peerIndex + 1, pPeer->nickname, 0, 100, 200);
                }
            }
            else if (pReceivedPacket[0] == PACKET_TYPE_RESEND) {
                unsigned int snDiff = sentPacketSeqNum - *(unsigned int*)(pReceivedPacket + 5);
                if (snDiff >= MAX_DROPPED_PACKETS) continue;
                //note: packet is already encrypted
                unsigned int requestedPacketIndex = (sentPacketIndex + MAX_DROPPED_PACKETS - snDiff) % MAX_DROPPED_PACKETS;
                send(pPeer->socket, (char*)(sentPackets + requestedPacketIndex * 65536), sentPacketLengths[requestedPacketIndex], 0);
            }
            //end of packet processing
        }

    }
    //remove peer
    closesocket(pPeer->socket);
    pPeer->socket = INVALID_SOCKET;
    ui_edit_user_name((short)peerIndex + 1, 0, 0, 0, 0);
    if (soundBuffer != NULL)
        soundBuffer->lpVtbl->Release(soundBuffer);
    free(recvBuf);
}


#ifdef _DEBUG
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
#else
void WinMainCRTStartup() {
#endif
	for (int i = 0; i < MAX_PEERS; i++)
		peers[i].socket = INVALID_SOCKET;

    if (FindWindowA("Meow", NULL) != NULL)
        fatalError("Already running");

	ui_init(Callback_SendChatMessage);
	ui_add_message("Meow! Type /help for a list of commands", 0, 0, 0, 0, 100, 100, 100);

	//open config or create if missing
	if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, (LPSTR)configPath) != S_OK)
		fatalError("failed to get appdata path");
	str_cat(configPath, "\\Catse");
	CreateDirectoryA(configPath, NULL);
	str_cat(configPath, "\\Meow.ini");
	HANDLE hFile;
	if ((hFile = CreateFileA(configPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE) {
		char defaultConfig[] = {
		"[General]\n"
		"MicChannels = 1\n"
		"MicBitDepth = 16\n"
		"MicSampleRate = 44100\n"
		"MicToggleKey = 120 ;F9 (https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)\n"
		"MicHoldKey = 121 ;F10\n"
		"MicNoiseGateThreshold = 0\n"
		"AudioTargetDelay = 250\n"
		"AudioTransmitInterval = 40\n"
		"Nickname = Anonymous\n"
		"DefaultChannel = None\n"
		"\n"
		"[Channels]\n"
		"ExampleName = catse.net:64771:0000000000000000:0000000000000000000000000000000000000000000000000000000000000000\n"
		};
		DWORD numWritten;
		WriteFile(hFile, defaultConfig, (DWORD)strlen(defaultConfig), &numWritten, NULL);
		CloseHandle(hFile);
		ui_add_message("Config file created", 0, 0, 0, 0, 100, 100, 100);
	}

	//parse config
	inifile config = iniParse(configPath);
	if (config.data == 0)
		fatalError("failed to parse config");
	cfg.numChannels = str_getint(iniGetValue(&config, "General", "MicChannels"));
	cfg.bitDepth = str_getint(iniGetValue(&config, "General", "MicBitDepth"));
	cfg.sampleRate = str_getint(iniGetValue(&config, "General", "MicSampleRate"));
	cfg.toggleKey = str_getint(iniGetValue(&config, "General", "MicToggleKey"));
	cfg.holdKey = str_getint(iniGetValue(&config, "General", "MicHoldKey"));
	cfg.noiseGateThreshold = str_getint(iniGetValue(&config, "General", "MicNoiseGateThreshold"));
	cfg.targetDelay = str_getint(iniGetValue(&config, "General", "AudioTargetDelay"));
	cfg.transmitInterval = str_getint(iniGetValue(&config, "General", "AudioTransmitInterval"));
	char* nickname = iniGetValue(&config, "General", "Nickname");
	memcpy(cfg.nickname, nickname, min(sizeof(cfg.nickname)-1, strlen(nickname)));
	char* channel = iniGetValue(&config, "Channels", iniGetValue(&config, "General", "DefaultChannel"));
    if (channel) {
        char* chAddr = channel, *chPort = strchr(channel, ':')+1, *chId = 0, *chKey = 0;
        if(chPort > (char*)1)
            chId = strchr(chPort, ':') + 1;
        if(chId > (char*)1)
            chKey = strchr(chId, ':')+1;
        if (chKey > (char*)1 && (chKey-chId-1) == 16) {
            memcpy(cfg.address, chAddr, min(sizeof(cfg.address) - 1, chPort - chAddr - 1));
            memcpy(cfg.port, chPort, min(sizeof(cfg.port) - 1, chId - chPort - 1));
            char hex[3] = { 0 };
            for (int i = 0; i < 8; i++) {
                hex[0] = chId[i*2+0];
                hex[1] = chId[i*2+1];
                cfg.id[i] = str_gethex(hex, 0);
            }
            for (int i = 0; i < 32; i++) {
                hex[0] = chKey[i*2+0];
                hex[1] = chKey[i*2+1];
                cfg.key[i] = str_gethex(hex, 0);
            }
        }
        else {
            ui_add_message("Error: bad cfg channel format", 0, 0, 0, 0, 200, 0, 0);
        }
    }


	AES_256_Key_Expansion(cfg.key, keyScheduleEnc, keyScheduleDec);
    ui_edit_user_name(0, cfg.nickname, 100, 200, 200);
    if (DirectSoundCreate8(NULL, &directSoundInterface, NULL) != DS_OK)
        fatalError("DirectSoundCreate8");
	if (directSoundInterface->lpVtbl->SetCooperativeLevel(directSoundInterface, GetDesktopWindow(), DSSCL_NORMAL) != DS_OK)
        fatalError("SetCooperativeLevel");
	if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AudioCaptureThread, NULL, 0, NULL) == NULL)
        fatalError("CreateThread");

    //initialize winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        fatalError("WSAStartup");
    //resolve server address
    struct addrinfo* serverAddr = NULL;
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(cfg.address, cfg.port, &hints, &serverAddr) != 0) {
        ui_add_message("Error: Failed to resolve server address", 0, 0, 0, 0, 200, 0, 0);
        Sleep(INFINITE);
    }
    //
    SOCKET serverSocket = INVALID_SOCKET;
    char* recvBuf = malloc(65536);
    if (recvBuf == NULL)
        fatalError("malloc fail");
    int recvLen = SOCKET_ERROR;
    SOCKADDR_IN peerAddr;
    peerAddr.sin_family = AF_INET;
    int timeoutCount = 1;
    while (1) {
        if (serverSocket == INVALID_SOCKET) { //create a new socket for connecting to server if the socket was assigned to a peer
            serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (serverSocket == INVALID_SOCKET) break;
            DWORD optval = 2000;
            if (setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&optval, sizeof(optval)) != 0) break;
            if (connect(serverSocket, serverAddr->ai_addr, (int)serverAddr->ai_addrlen) != 0) break;
        }
        if (recvLen == SOCKET_ERROR && send(serverSocket, cfg.id, sizeof(cfg.id), 0) == SOCKET_ERROR) break; //ping sv on first run and on receive timeouts
        recvLen = recv(serverSocket, recvBuf, 65536, 0);
        if (timeoutCount > 1 && recvLen != SOCKET_ERROR)
            ui_add_message("Peer discovery server online", 0, 0, 0, 0, 100, 100, 100);
        timeoutCount = (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) ? timeoutCount + 1 : 0;
        if (timeoutCount == 2)
            ui_add_message("Peer discovery server offline", 0, 0, 0, 0, 100, 100, 100);
        if (recvLen == SOCKET_ERROR && !timeoutCount) break; //error occured that is not a timeout
        if (recvLen != 6) continue;
        int found = 0; //discovered a peer, check that it doesn't already exist
        for (found = MAX_PEERS; found > 0 && (peers[found - 1].socket == INVALID_SOCKET || peers[found - 1].ip != *(unsigned int*)recvBuf); found--);
        for (int i = 0; i < MAX_PEERS && !found; i++) {
            if (peers[i].socket == INVALID_SOCKET) {
                peers[i].socket = serverSocket;
                peers[i].ip = *(unsigned int*)recvBuf;
                serverSocket = INVALID_SOCKET;
                recvLen = SOCKET_ERROR;
                memcpy(&peerAddr.sin_addr.s_addr, recvBuf, 4);
                memcpy(&peerAddr.sin_port, recvBuf + 4, 2);
                if (connect(peers[i].socket, (SOCKADDR*)&peerAddr, sizeof(peerAddr)) == SOCKET_ERROR || CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PeerThread, &i, 0, NULL) == NULL) {
                    closesocket(peers[i].socket);
                    peers[i].socket = INVALID_SOCKET;
                    break;
                }
                ui_edit_user_name((short)i + 1, "Connecting...", 100, 100, 100);
                break;
            }
        }
    }
    ui_add_message("Peer discovery stopped due to an error", 0, 0, 0, 0, 100, 100, 100);
    if (serverSocket != INVALID_SOCKET) {
        shutdown(serverSocket, SD_SEND);
        closesocket(serverSocket);
    }
    freeaddrinfo(serverAddr);
	

	Sleep(INFINITE);
	
}

