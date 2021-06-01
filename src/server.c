#ifdef __unix__
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define SV_MAX_PEERS 128

typedef struct {
    unsigned int	ip;
    unsigned short	port;
    time_t			timestamp;
    unsigned int	connected[SV_MAX_PEERS];
    unsigned int	connectedCount;
}sv_peer;

sv_peer			sv_peers[SV_MAX_PEERS];
unsigned int	sv_peerCount = 0;

#ifdef __unix__
typedef int SOCKET;
#define INVALID_SOCKET -1;
void main() {
    unsigned short port = 64771;
#else
void StartServer(unsigned short port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
#endif
    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) return;
    struct sockaddr_in fromAddr;
    fromAddr.sin_family = AF_INET;
    fromAddr.sin_port = htons(port);
    fromAddr.sin_addr.s_addr = INADDR_ANY;
    struct sockaddr_in toAddr;
    toAddr.sin_family = AF_INET;
    int addrLen = sizeof(struct sockaddr_in);
    if (bind(serverSocket, (struct sockaddr*) & fromAddr, addrLen) != 0) return;
    unsigned char* recvBuf = malloc(65536);
    if (recvBuf == 0) return;
    printf("server started on port %hu\n", port);
    while (1) {
        //receive data
        int recvLen = recvfrom(serverSocket, (char*)recvBuf, 65536, 0, (struct sockaddr*) & fromAddr, &addrLen);
        if (recvLen == -1) continue;
        if (*(int*)recvBuf != *(int*)"meow") continue;
        //clear offline peers
        time_t curTime = time(NULL);
        for (unsigned int i = 0; i < sv_peerCount; i++) {
            if (curTime > sv_peers[i].timestamp + 3) {
                //remove from connected lists
                for (unsigned int j = 0; j < sv_peerCount; j++) {
                    for (unsigned int k = 0; k < sv_peers[j].connectedCount; k++) {
                        if (sv_peers[j].connected[k] == sv_peers[i].ip) {
                            sv_peers[j].connectedCount--;
                            sv_peers[j].connected[k] = sv_peers[j].connected[sv_peers[j].connectedCount];
                            break;
                        }
                    }
                }
                //remove from peers list
                sv_peerCount--;
                sv_peers[i] = sv_peers[sv_peerCount];
                i--;
            }
        }
        //update / create peer info
        unsigned int senderIndex = 0;
        for (; senderIndex < sv_peerCount && sv_peers[senderIndex].ip != fromAddr.sin_addr.s_addr; senderIndex++);
        if (senderIndex == sv_peerCount) {
            if (sv_peerCount == SV_MAX_PEERS) continue;
            sv_peers[senderIndex].ip = fromAddr.sin_addr.s_addr;
            sv_peers[senderIndex].connectedCount = 0;
            sv_peerCount++;
        }
        sv_peers[senderIndex].port = fromAddr.sin_port;
        sv_peers[senderIndex].timestamp = curTime;
        //try to find a peer that has a free port that hasn't connected with the sender yet
        for (unsigned int otherPeerIndex = 0; otherPeerIndex < sv_peerCount && sv_peers[senderIndex].port != 0; otherPeerIndex++) {
            if (otherPeerIndex == senderIndex || sv_peers[otherPeerIndex].port == 0) continue;
            for (unsigned int i = 0; i <= sv_peers[otherPeerIndex].connectedCount; i++) {
                if (i == sv_peers[otherPeerIndex].connectedCount) {
                    //send other peer info to sender
                    if (sendto(serverSocket, (char*)&sv_peers[otherPeerIndex], 6, 0, (struct sockaddr*) & fromAddr, addrLen) == -1) continue;
                    //send sender info to other peer
                    toAddr.sin_addr.s_addr = sv_peers[otherPeerIndex].ip;
                    toAddr.sin_port = sv_peers[otherPeerIndex].port;
                    if (sendto(serverSocket, (char*)&sv_peers[senderIndex], 6, 0, (struct sockaddr*) & toAddr, addrLen) == -1) continue;
                    //mark them as connected to each other
                    sv_peers[senderIndex].connected[sv_peers[senderIndex].connectedCount] = sv_peers[otherPeerIndex].ip;
                    sv_peers[senderIndex].connectedCount++;
                    sv_peers[senderIndex].port = 0;
                    sv_peers[otherPeerIndex].connected[sv_peers[otherPeerIndex].connectedCount] = sv_peers[senderIndex].ip;
                    sv_peers[otherPeerIndex].connectedCount++;
                    sv_peers[otherPeerIndex].port = 0;
                    break;
                }
                if (sv_peers[otherPeerIndex].connected[i] == sv_peers[senderIndex].ip) break;
            }
        }
        if (sv_peers[senderIndex].port) sendto(serverSocket, "", 1, 0, (struct sockaddr*) & fromAddr, addrLen);
    }
}