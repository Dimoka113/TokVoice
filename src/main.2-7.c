#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <portaudio.h>
#include "voice_proto.h"

#define SAMPLE_RATE 16000
#define FRAMES_PER_BUFFER 320
#define PORT 3030
#define MAX_PACKET 2048

int sock;
struct sockaddr_in servaddr;
PaStream *inputStream, *outputStream;
volatile int mic_muted = 0;
volatile int voice_muted = 0;

static uint32_t seq_counter = 0;
static uint16_t client_id = 0;

void init_client_id() {
    srand((unsigned)time(NULL));
    client_id = (uint16_t)(rand() & 0xFFFF);
}

void* mic_thread(void* arg) {
    size_t pcm_bytes = sizeof(short) * FRAMES_PER_BUFFER;
    size_t packet_size = sizeof(VoicePacketHeader) + pcm_bytes;

    char *packet = malloc(packet_size);
    short *buffer = (short*)(packet + sizeof(VoicePacketHeader));

    while (1) {
        if (Pa_ReadStream(inputStream, buffer, FRAMES_PER_BUFFER) != paNoError)
            continue;

        if (!mic_muted) {
            VoicePacketHeader *hdr = (VoicePacketHeader*)packet;
            hdr->magic = VOICE_MAGIC;
            hdr->version = VOICE_VERSION;
            hdr->client_id = client_id;
            hdr->seq = seq_counter++;
            hdr->timestamp = (uint64_t)time(NULL);
            hdr->payload_len = (uint16_t)pcm_bytes;

            sendto(sock, packet, packet_size, 0,
                   (struct sockaddr*)&servaddr, sizeof(servaddr));
        }
    }
    return NULL;
}

void* speaker_thread(void* arg) {
    char packet[MAX_PACKET];
    while (1) {
        int n = recvfrom(sock, packet, MAX_PACKET, 0, NULL, NULL);
        if (n <= (int)sizeof(VoicePacketHeader)) continue;

        VoicePacketHeader *hdr = (VoicePacketHeader*)packet;
        if (hdr->magic != VOICE_MAGIC || hdr->version != VOICE_VERSION) continue;
        if (hdr->client_id == client_id) continue; // не играть свой звук
        if (hdr->payload_len + sizeof(VoicePacketHeader) > n) continue;

        short *pcm = (short*)(packet + sizeof(VoicePacketHeader));
        if (!voice_muted) {
            Pa_WriteStream(outputStream, pcm, FRAMES_PER_BUFFER);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    init_client_id();

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);

    Pa_Initialize();
    Pa_OpenDefaultStream(&inputStream, 1, 0, paInt16,
                         SAMPLE_RATE, FRAMES_PER_BUFFER, NULL, NULL);
    Pa_OpenDefaultStream(&outputStream, 0, 1, paInt16,
                         SAMPLE_RATE, FRAMES_PER_BUFFER, NULL, NULL);
    Pa_StartStream(inputStream);
    Pa_StartStream(outputStream);

    pthread_t mic, spk;
    pthread_create(&mic, NULL, mic_thread, NULL);
    pthread_create(&spk, NULL, speaker_thread, NULL);

    printf("Connected to %s:%d as client_id=%d\n",
           argv[1], PORT, client_id);
    printf("Press Ctrl+C to exit.\n");

    pthread_join(mic, NULL);
    pthread_join(spk, NULL);

    Pa_Terminate();
    close(sock);
    return 0;
}