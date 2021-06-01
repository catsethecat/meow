#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <time.h>
#include <WinSock2.h> //includes windows.h
#include <WS2tcpip.h>
#include <initguid.h>
#include <mmreg.h>
#include <dsound.h>

#include "aes_256.c"
#include "client_ui.c"
#include "server.c"

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
    char            address[32];
    char            port[8];
    unsigned int    targetDelay;
    unsigned int    transmitInterval;
    unsigned int    noiseGateThreshold;
}cfg;

unsigned char   keyScheduleEnc[240], keyScheduleDec[240], initVec[16];
LPDIRECTSOUND8  directSoundInterface;
unsigned char   sentPackets[MAX_DROPPED_PACKETS*65536];
unsigned short  sentPacketLengths[MAX_DROPPED_PACKETS];
unsigned int    sentPacketIndex = 0;
unsigned int    sentPacketSeqNum = 0;
int             debug_droptest = 0;

void Callback_SendChatMessage(char* msg, int msgLen) {
    //process commands
    if (*msg == '/') {
        int     argc = 0;
        char*   argv[3] = { 0 };
        for(int i = 0; i < msgLen; i++){
            if (msg[i] == ' ') {
                argv[argc] = msg + i + 1;
                argc++;
            }
        }
        //command: /vol [string peername] [float volume]
        if (*(int*)msg == *(int*)"/vol" && argc == 2) {
            for (unsigned int i = 0; i < MAX_PEERS; i++) {
                if (peers[i].socket != INVALID_SOCKET && memcmp(peers[i].nickname, argv[0], min(strlen(peers[i].nickname), (size_t)(argv[1]-argv[0]-1))) == 0) {
                    peers[i].volume = strtof(argv[1], NULL);
                    return;
                }
            }
        }
        //command: /ng [int threshold]
        if (*(int*)msg == *(int*)"/ng " && argc > 0) {
            cfg.noiseGateThreshold = atoi(argv[0]);
            return;
        }
        //command: /drop [int count]
        if (*(int*)msg == *(int*)"/dro") {
            debug_droptest = 1;
            if (argc > 0) debug_droptest = atoi(argv[0]);
            return;
        }
    }
    //display & send message
    meow_ui_new_message(msg, cfg.nickname, 0);
    unsigned char* sendBuf = sentPackets + sentPacketIndex * 65536;
    sendBuf[0] = PACKET_TYPE_CHAT;
    *((unsigned int*)(sendBuf+1)) = sentPacketSeqNum;
    memcpy(sendBuf + 5, msg, msgLen + 1); //+1 to include null
    int sendLen = CEIL16(5 + msgLen + 1);
    AES_256_CBC_encrypt(sendBuf, sendLen, initVec, keyScheduleEnc);
    sentPacketLengths[sentPacketIndex] = (unsigned short)sendLen;
    sentPacketSeqNum++;
    sentPacketIndex = (sentPacketIndex+1) % MAX_DROPPED_PACKETS;
    if (debug_droptest) {
        debug_droptest--;
        return;
    }
    for (int i = 0; i < MAX_PEERS; i++) {
        if (peers[i].socket != INVALID_SOCKET) {
            if (send(peers[i].socket, (char*)sendBuf, sendLen, 0) == SOCKET_ERROR) {
                meow_ui_new_message("\4Error: send()", "", 0);
            }
        }
    }
}

void AudioCaptureThread() {
    //create IDirectSoundCapture8 interface
    LPDIRECTSOUNDCAPTURE8 capture = NULL;
    HRESULT res = DirectSoundCaptureCreate8(NULL, &capture, NULL);
    if (res != DS_OK) {
        meow_ui_new_message("\4Error: DirectSoundCaptureCreate8", "", 0);
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
        meow_ui_new_message("\4Error: CreateCaptureBuffer", "", 0);
        return;
    }
    //get IDirectSoundCaptureBuffer8
    LPDIRECTSOUNDCAPTUREBUFFER8 captureBuffer = NULL;
    res = tmpbuffer->lpVtbl->QueryInterface(tmpbuffer, &IID_IDirectSoundCaptureBuffer8, (LPVOID*)&captureBuffer);
    if (res != DS_OK) {
        meow_ui_new_message("\4Error: QueryInterface", "", 0);
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
                meow_ui_update_peerlist(1, cfg.nickname, 3);
                res = captureBuffer->lpVtbl->Start(captureBuffer, DSCBSTART_LOOPING);
                if (res != DS_OK) {
                    meow_ui_new_message("\4Error: capture start", "", 0);
                    return;
                }
            }
            else {
                meow_ui_update_peerlist(1, cfg.nickname, 11);
                notifyStop = 1;
            }
        }
        pressedKeys = pressingKeys;
        //
        if (recording || notifyStop) {
            if (captureBuffer->lpVtbl->GetCurrentPosition(captureBuffer, NULL, &cbPos) != DS_OK) {
                meow_ui_new_message("\4Error: capture getcurrentposition", "", 0);
                continue;
            }
            cbBytesToLock = (cbPos >= cbLastPos) ? (cbPos - cbLastPos) : (bufferdesc.dwBufferBytes - cbLastPos + cbPos);
            if (cbBytesToLock == 0) {
                Sleep(cfg.transmitInterval);
                continue;
            }
            if (captureBuffer->lpVtbl->Lock(captureBuffer, cbLastPos, cbBytesToLock, &cbPtr1, &cbLen1, &cbPtr2, &cbLen2, 0) != DS_OK) {
                meow_ui_new_message("\4Error: capture lock", "", 0);
                continue;
            }
            cbLastPos = cbPos;
            if (cbLen1 + cbLen2 > 65000) {
                meow_ui_new_message("\4Error: captured too much data", "", 0);
                continue;
            }
            //build packet
            unsigned char* sendBuf = sentPackets + sentPacketIndex * 65536;
            sendBuf[0] = PACKET_TYPE_AUDIO;
            *((unsigned int*)(sendBuf+1)) = sentPacketSeqNum;
            *((unsigned short*)(sendBuf + 5)) = (unsigned short)(cbLen1 + cbLen2);
            memcpy(sendBuf + 7, cbPtr1, cbLen1);
            memcpy(sendBuf + 7 + cbLen1, cbPtr2, cbLen2);
            int sendLen = CEIL16(7 + cbLen1 + cbLen2);
            if (captureBuffer->lpVtbl->Unlock(captureBuffer, cbPtr1, cbLen1, cbPtr2, cbLen2) != DS_OK) {
                meow_ui_new_message("\4Error: capture unlock", "", 0);
                return;
            }
            //audio processing
            if (cfg.noiseGateThreshold != 0 && cfg.bitDepth == 16) {
                for (short* sample = (short*)(sendBuf+7); sample < (short*)(sendBuf + sendLen); sample++){
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
                    meow_ui_new_message("\4Error: capture stop", "", 0);
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
                sentPacketIndex = (sentPacketIndex+1) % MAX_DROPPED_PACKETS;
                if (debug_droptest) {
                    debug_droptest--;
                    continue;
                }
                for (int i = 0; i < MAX_PEERS; i++) {
                    if (peers[i].socket != INVALID_SOCKET) {
                        if (send(peers[i].socket, (char*)sendBuf, sendLen, 0) == SOCKET_ERROR) {
                            meow_ui_new_message("\4Error: capture send", "", 0);
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
    DWORD	sbTotalLen  = 0;
    DWORD	sbPos       = 0;
    DWORD	sbLastPos   = 0;
    LPVOID	sbPtr1      = 0;
    DWORD	sbLen1      = 0;
    LPVOID	sbPtr2      = 0;
    DWORD	sbLen2      = 0;
    int		sbPlaying   = 0;
    int		sbDelay     = 0;
    int     sbTargetDelay  = 0;
    int     triggerAudioPause = 0;
    unsigned char* recvBuf      = malloc(MAX_DROPPED_PACKETS*65536);
    unsigned int   recvBufCount = 0;
    unsigned int   recvBufIndex = 0;
    int     recvLen     = 0;
    int     recvTimeoutCount = 0;
    time_t  keepAliveTime = time(NULL);
    unsigned int expectedSeqNum = 0;
    //exchange peer info / initialize peer
    unsigned char clientInfo[48];
    clientInfo[0] = PACKET_TYPE_INFO;
    *((unsigned int*)(clientInfo+1)) = sentPacketSeqNum;
    memcpy(clientInfo + 5, cfg.nickname, 16);
    ((unsigned int*)(clientInfo + 5 + 16))[0] = cfg.numChannels;
    ((unsigned int*)(clientInfo + 5 + 16))[1] = cfg.bitDepth;
    ((unsigned int*)(clientInfo + 5 + 16))[2] = cfg.sampleRate;
    AES_256_CBC_encrypt(clientInfo, sizeof(clientInfo), initVec, keyScheduleEnc);
    int success = recvBuf != 0 && (send(pPeer->socket, (char*)clientInfo, sizeof(clientInfo), 0) != SOCKET_ERROR) && (recv(pPeer->socket, (char*)recvBuf, 65536, 0) == sizeof(clientInfo)) && AES_256_CBC_decrypt(recvBuf, sizeof(clientInfo), initVec, keyScheduleDec) && memcpy(pPeer, recvBuf + 5, 28) && !recvBuf[0] && ISVALID_NUMCHANNELS(pPeer->numChannels) && ISVALID_BITDEPTH(pPeer->bitDepth) && ISVALID_SAMPLERATE(pPeer->sampleRate);
    if(!success) meow_ui_new_message("\4Error: Failed to communicate with peer", "", 0);
    while (success) {
        success = 0;
        expectedSeqNum = *((unsigned int*)(recvBuf + 1));
        //resend our info in case the peer didnt receive the first one
        if (send(pPeer->socket, (char*)clientInfo, sizeof(clientInfo), 0) == SOCKET_ERROR) {
            meow_ui_new_message("\4Error: peer send", "", 0);
            break;
        }
        //sanitize nickname
        int i;
        for (i = 0; i < 16 && pPeer->nickname[i] >= 32 && pPeer->nickname[i] <= 126; i++);
        pPeer->nickname[i] = 0;
        if (i == 0) memcpy(pPeer->nickname, "Anonymous", 10);
        meow_ui_update_peerlist((short)peerIndex + 2, pPeer->nickname, 11);
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
            meow_ui_new_message("\4Error: peer createsoundbuffer", "", 0);
            break;
        }
        //get IDirectSoundBuffer8
        if (tmpbuffer->lpVtbl->QueryInterface(tmpbuffer, &IID_IDirectSoundBuffer8, (LPVOID*)&soundBuffer) != DS_OK) {
            meow_ui_new_message("\4Error: peer QueryInterface", "", 0);
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
        time_t curTime = time(NULL);
        if (curTime > keepAliveTime + 1) {
            keepAliveTime = curTime;
            unsigned char tmp[16];
            tmp[0] = PACKET_TYPE_KEEPALIVE;
            *((unsigned int*)(tmp+1)) = sentPacketSeqNum;
            AES_256_CBC_encrypt(tmp, 16, initVec, keyScheduleEnc);
            if (send(pPeer->socket, (char*)tmp, 16, 0) == SOCKET_ERROR) {
                meow_ui_new_message("\4Error: peer send", "", 0);
                break;
            }
        }
        //receive
        unsigned char* pReceivedPacket = recvBuf + ((recvBufIndex + recvBufCount)%MAX_DROPPED_PACKETS) * 65536;
        recvLen = recv(pPeer->socket, (char*)pReceivedPacket, 65536, 0);
        if (recvLen == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                if (recvTimeoutCount > 0) {
                    meow_ui_new_message("\10Peer timed out", "", 0);
                    break;
                }
                recvTimeoutCount++;
                continue;
            }
            meow_ui_new_message("\4Error: peer recv", "", 0);
            break;
        }
        recvTimeoutCount = 0;
        if (recvLen % 16) continue;
        AES_256_CBC_decrypt(pReceivedPacket, recvLen, initVec, keyScheduleDec);
        unsigned int receivedSeqNum = *(unsigned int*)(pReceivedPacket + 1);
        if (receivedSeqNum < expectedSeqNum) continue;
        //store packet if its seqnum is higher than expected and higher than the previous stored seqnum (if there are previous stored packets in the queue)
        recvBufCount += (receivedSeqNum > expectedSeqNum) && (recvBufCount == 0 || receivedSeqNum > *(unsigned int*)(recvBuf + ((recvBufIndex + recvBufCount - 1)%MAX_DROPPED_PACKETS) * 65536 + 1));
        if (((receivedSeqNum-expectedSeqNum) >= MAX_DROPPED_PACKETS) || (recvBufCount >= MAX_DROPPED_PACKETS)) {
            meow_ui_new_message("\10Dropped too many packets, skipping ahead...", "", 0);
            expectedSeqNum = receivedSeqNum;
            recvBufCount = 0;
            triggerAudioPause = sbPlaying;
        }
        //first process current packet if its seqnum is expected, then process the backlog of stored packets
        for (int first = receivedSeqNum == expectedSeqNum; (recvBufCount+first)>0; first=0) {
            if (!first) pReceivedPacket = recvBuf + recvBufIndex * 65536;
            if (*(unsigned int*)(pReceivedPacket+1) != expectedSeqNum) {
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
                for (i = 1; i < 256 && pReceivedPacket[5+i] > 15; i++);
                pReceivedPacket[5+i] = 0;
                if (i > 0) meow_ui_new_message((char*)pReceivedPacket + 5, pPeer->nickname, peerIndex+1);
            }
            else if (pReceivedPacket[0] == PACKET_TYPE_AUDIO) {
                unsigned int recvAudioLen = *((unsigned short*)(pReceivedPacket + 5));
                if (recvAudioLen > sbTotalLen / 2) {
                    meow_ui_new_message("\4Error: peer audio recvlen >500ms", "", 0);
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
                        meow_ui_new_message("\4Error: peer lock", "", 0);
                        success = 0;
                        break;
                    }
                    memcpy(sbPtr1, pReceivedPacket + 7, sbLen1);
                    memcpy(sbPtr2, pReceivedPacket + 7 + sbLen1, sbLen2);
                    if (soundBuffer->lpVtbl->Unlock(soundBuffer, sbPtr1, sbLen1, sbPtr2, sbLen2) != DS_OK) {
                        meow_ui_new_message("\4Error: peer unlock", "", 0);
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
                    meow_ui_new_message("\4Error: peer getcurrentposition", "", 0);
                    success = 0;
                    break;
                }
                if (sbLastPos >= sbPos) sbDelay = sbLastPos - sbPos;
                else sbDelay = sbTotalLen - sbPos + sbLastPos;
                if (sbPlaying && (recvAudioLen == 0 || triggerAudioPause)) {
                    triggerAudioPause = 0;
                    if (soundBuffer->lpVtbl->Stop(soundBuffer) != DS_OK) {
                        meow_ui_new_message("\4Error: peer stop", "", 0);
                        success = 0;
                        break;
                    }
                    if (soundBuffer->lpVtbl->SetCurrentPosition(soundBuffer, sbLastPos) != DS_OK) {
                        meow_ui_new_message("\4Error: peer setcurrentposition", "", 0);
                        success = 0;
                        break;
                    }
                    sbPlaying = 0;
                    sbDelay = 0;
                    meow_ui_update_peerlist((short)peerIndex + 2, pPeer->nickname, 11);
                }
                if (!sbPlaying && sbDelay >= sbTargetDelay) {
                    if (soundBuffer->lpVtbl->Play(soundBuffer, 0, 0, DSBPLAY_LOOPING) != DS_OK) {
                        meow_ui_new_message("\4Error: peer play", "", 0);
                        success = 0;
                        break;
                    }
                    sbPlaying = 1;
                    meow_ui_update_peerlist((short)peerIndex + 2, pPeer->nickname, 3);
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
    meow_ui_update_peerlist((short)peerIndex+2, 0, 0);
    if (soundBuffer != NULL)
        soundBuffer->lpVtbl->Release(soundBuffer);
    free(recvBuf);
}

int main(int argc, char** argv) {
    SetConsoleTitleA("Meow [early access prototype 11]");
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-server") == 0) {
            StartServer((unsigned short)(i < (argc-1) ? atoi(argv[i+1]) : 64771));
            return 0;
        }
    }
    for (int i = 0; i < MAX_PEERS; i++) peers[i].socket = INVALID_SOCKET;
    g_ui.sendChatMessageFunc = &Callback_SendChatMessage;
    meow_ui_init();
    //open config or create if missing
    char defaultConfig[] = {
        "Address=0.0.0.0\n"
        "Port=64771\n"
        "MicChannels=1\n"
        "MicBitDepth=16\n"
        "MicSampleRate=44100\n"
        "MicToggleKey=120 //F9 (https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)\n"
        "MicHoldKey=121 //F10\n"
        "MicNoiseGateThreshold=0\n"
        "AudioTargetDelay=250\n"
        "AudioTransmitInterval=40\n"
        "Nickname=Anonymous\n"
        "Key=0000000000000000000000000000000000000000000000000000000000000000"
    };
    FILE* configFile = NULL;
    if (fopen_s(&configFile, "config.txt", "r") != 0) {
        meow_ui_new_message("Failed to open config.txt, creating...", "", 0);
        if (fopen_s(&configFile, "config.txt", "w") != 0 || fwrite(defaultConfig, 1, sizeof(defaultConfig)-1, configFile) != sizeof(defaultConfig)-1 || fclose(configFile) != 0)
            meow_ui_new_message("Failed to create config.txt", "", 0);
        meow_ui_new_message("Please restart the program to continue", "", 0);
        Sleep(INFINITE);
    }
    //parse config
    int itemsFilled = fscanf_s(configFile, "%*[^=]=%32s%*[^=]=%8s%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%u%*[^=]=%15s%*[^=]=",
        cfg.address, 32, cfg.port, 8, &cfg.numChannels, &cfg.bitDepth, &cfg.sampleRate, &cfg.toggleKey, &cfg.holdKey, &cfg.noiseGateThreshold, &cfg.targetDelay, &cfg.transmitInterval, cfg.nickname, 16);
    for (int i = 0; i < 32 && fscanf_s(configFile, "%2hhx", cfg.key+i) == 1; i++) itemsFilled++;
    if (itemsFilled != 11 + 32) {
        meow_ui_new_message("Error: Failed to parse config.txt", "", 0);
        Sleep(INFINITE);
    }
    fclose(configFile);
    //
    AES_256_Key_Expansion(cfg.key, keyScheduleEnc, keyScheduleDec);
    meow_ui_update_peerlist(1, cfg.nickname, 11);
    if (DirectSoundCreate8(NULL, &directSoundInterface, NULL) != DS_OK) return 0;
    if (directSoundInterface->lpVtbl->SetCooperativeLevel(directSoundInterface, GetDesktopWindow(), DSSCL_NORMAL) != DS_OK) return 0;
    if (CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AudioCaptureThread, NULL, 0, NULL) == NULL) return 0;
    //initialize winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 0;
    //resolve server address
    struct addrinfo* serverAddr = NULL;
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    if (getaddrinfo(cfg.address, cfg.port, &hints, &serverAddr) != 0) {
        meow_ui_new_message("Error: Failed to resolve server address", "", 0);
        Sleep(INFINITE);
    }
    //
    SOCKET serverSocket = INVALID_SOCKET;
    char* recvBuf = malloc(65536);
    if (recvBuf == NULL) return 0;
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
        if (recvLen == SOCKET_ERROR && send(serverSocket, "meow", 4, 0) == SOCKET_ERROR) break; //ping sv on first run and on receive timeouts
        recvLen = recv(serverSocket, recvBuf, 65536, 0);
        if (timeoutCount > 1 && recvLen != SOCKET_ERROR) meow_ui_new_message("\10Peer discovery server online", "", 0);
        timeoutCount = (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) ? timeoutCount + 1 : 0;
        if(timeoutCount == 2) meow_ui_new_message("\10Peer discovery server offline", "", 0);
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
                meow_ui_update_peerlist((short)i + 2, "Connecting...", 8);
                break;
            }
        }
    }
    meow_ui_new_message("\10Peer discovery stopped due to an error", "", 0);
    if (serverSocket != INVALID_SOCKET) {
        shutdown(serverSocket, SD_SEND);
        closesocket(serverSocket);
    }
    freeaddrinfo(serverAddr);
    Sleep(INFINITE);
}

