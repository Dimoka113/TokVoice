#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <mmsystem.h>
#include <portaudio.h>
#include <io.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #define CLOSESOCKET closesocket
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <pthread.h>
    #include <arpa/inet.h>
    #include <termios.h>
    #include <fcntl.h>
    #define CLOSESOCKET close
#endif

#include <portaudio.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "portaudio_x86.lib")

// ---------------- CONFIG ----------------
typedef struct {
    int main_key;
    int modifiers[3];
    int mod_count;
} Hotkey;

typedef struct {
    char server_ip[64];
    int server_port;
    int sample_rate;
    int frames_per_buffer;
    Hotkey mute_key;
    Hotkey voice_mute_key;
    char mic_mute_sound[256];
    char mic_unmute_sound[256];
    char voice_mute_sound[256];
    char voice_unmute_sound[256];
} Config;

Config cfg;
int mic_muted = 0;
int voice_muted = 0;

// -------------------- MIC THREAD --------------------
#ifdef _WIN32
DWORD WINAPI mic_thread(LPVOID arg)
#else
void* mic_thread(void* arg)
#endif
{
    short *buffer = malloc(sizeof(short) * cfg.frames_per_buffer);
    while (1) {
        Pa_ReadStream(inputStream, buffer, cfg.frames_per_buffer);
        if (!mic_muted)
            sendto(sock, (const char*)buffer, sizeof(short) * cfg.frames_per_buffer, 0,
                   (struct sockaddr*)&servaddr, sizeof(servaddr));
    }
    return 0;
}

// -------------------- SPEAKER THREAD --------------------
#ifdef _WIN32
DWORD WINAPI speaker_thread(LPVOID arg)
#else
void* speaker_thread(void* arg)
#endif
{
    short *buffer = malloc(sizeof(short) * cfg.frames_per_buffer);
    while (1) {
        recv(sock, (char*)buffer, sizeof(short) * cfg.frames_per_buffer, 0);
        if (!voice_muted)
            Pa_WriteStream(outputStream, buffer, cfg.frames_per_buffer);
    }
    return 0;
}

// ---------------- COLORS ----------------
#define RESET   "\033[0m"

#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

#define BRIGHT_RED   "\033[91m"
#define BRIGHT_GREEN "\033[92m"
#define BRIGHT_BLUE  "\033[94m"

// ---------------- PARSING KEYS ----------------
const char* key_to_string(int code) {
    switch (code) {
        case VK_MENU: return "ALT";
        case VK_CONTROL: return "CTRL";
        case VK_SHIFT: return "SHIFT";
        default: break;
    }
    static char buf[8];
    if (code >= 0x41 && code <= 0x5A) { // A-Z
        snprintf(buf, sizeof(buf), "%c", code);
        return buf;
    }
    if (code >= 0x30 && code <= 0x39) { // 0-9
        snprintf(buf, sizeof(buf), "%c", code);
        return buf;
    }
    snprintf(buf, sizeof(buf), "?");
    return buf;
}

char* hotkey_to_string(Hotkey hk, char *out, size_t out_size) {
    out[0] = 0;
    for (int i=0; i<hk.mod_count; i++) {
        strcat(out, key_to_string(hk.modifiers[i]));
        strcat(out, "+");
    }
    strcat(out, key_to_string(hk.main_key));
    return out;
}

int key_from_string(const char *s) {
    if (!s || !s[0]) return 0;
    if (strcmp(s, "ALT") == 0) return VK_MENU;
    if (strcmp(s, "CTRL") == 0) return VK_CONTROL;
    if (strcmp(s, "SHIFT") == 0) return VK_SHIFT;
    if (s[0] >= 'A' && s[0] <= 'Z') return 0x41 + (s[0]-'A');
    if (s[0] >= 'a' && s[0] <= 'z') return 0x41 + (s[0]-'a');
    if (s[0] >= '0' && s[0] <= '9') return 0x30 + (s[0]-'0');
    return 0;
}

Hotkey parse_hotkey(const char *str) {
    Hotkey hk = {0};
    hk.mod_count = 0;
    char tmp[128];
    strncpy(tmp, str, sizeof(tmp)); tmp[sizeof(tmp)-1] = 0;

    char *token = strtok(tmp, "+");
    while (token) {
        int code = key_from_string(token);
        if (!code) { token = strtok(NULL, "+"); continue; }
        if (code == VK_CONTROL || code == VK_SHIFT || code == VK_MENU) {
            hk.modifiers[hk.mod_count++] = code;
        } else {
            hk.main_key = code;
        }
        token = strtok(NULL, "+");
    }
    return hk;
}

int is_hotkey_pressed(Hotkey hk) {
    for (int i=0; i<hk.mod_count; i++)
        if (!(GetAsyncKeyState(hk.modifiers[i]) & 0x8000)) return 0;
    if (!(GetAsyncKeyState(hk.main_key) & 0x8000)) return 0;
    return 1;
}

// ---------------- CONFIG FILE ----------------
int parse_config(const char *filename, Config *c) {
    FILE *f = fopen(filename, "r");
    if (!f) { fprintf(stderr,"Config not found: %s\n", filename); return 0; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char key[128], val[256];
        if (sscanf(line, "%127[^=]=%255[^\n]", key, val) == 2) {
            if (strcmp(key,"server_ip")==0) strncpy(c->server_ip,val,sizeof(c->server_ip));
            else if (strcmp(key,"server_port")==0) c->server_port=atoi(val);
            else if (strcmp(key,"sample_rate")==0) c->sample_rate=atoi(val);
            else if (strcmp(key,"frames_per_buffer")==0) c->frames_per_buffer=atoi(val);
            else if (strcmp(key,"mute_key")==0) c->mute_key = parse_hotkey(val);
            else if (strcmp(key,"voice_mute_key")==0) c->voice_mute_key = parse_hotkey(val);
            else if (strcmp(key,"mic_mute_sound")==0) strncpy(c->mic_mute_sound,val,sizeof(c->mic_mute_sound));
            else if (strcmp(key,"mic_unmute_sound")==0) strncpy(c->mic_unmute_sound,val,sizeof(c->mic_unmute_sound));
            else if (strcmp(key,"voice_mute_sound")==0) strncpy(c->voice_mute_sound,val,sizeof(c->voice_mute_sound));
            else if (strcmp(key,"voice_unmute_sound")==0) strncpy(c->voice_unmute_sound,val,sizeof(c->voice_unmute_sound));
        }
    }
    fclose(f);
    return 1;
}

// ---------------- SOUND ----------------
DWORD WINAPI play_sound_thread(LPVOID arg) {
    char *path = (char*)arg;
    if (_access(path,4)!=0) { free(path); return 0; }
    PlaySoundA(path, NULL, SND_FILENAME | SND_ASYNC);
    free(path);
    return 0;
}

void play_sound_async(const char *path) {
    if (!path || !path[0]) return;
    char *copy = _strdup(path);
    CreateThread(NULL,0,play_sound_thread,copy,0,NULL);
}

// ---------------- UI UPDATE ----------------
void print_status() {
    system("cls");

    char buf1[64], buf2[64];
    printf("%sToksVoice%s: %s%s:%d%s\n",
       BRIGHT_GREEN, RESET, CYAN, cfg.server_ip, cfg.server_port, RESET);

    printf("Sample rate: %s%d\n%s", CYAN, cfg.sample_rate);
    printf("Frames/buf: %s%d\n%s", cfg.frames_per_buffer);

    printf("Mic key: %s\n", hotkey_to_string(cfg.mute_key, buf1, sizeof(buf1)));
    printf("Voice key: %s\n", hotkey_to_string(cfg.voice_mute_key, buf2, sizeof(buf2)));
    
    printf("%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-\n%s", 
        BRIGHT_GREEN, GREEN, BRIGHT_GREEN, GREEN, BRIGHT_GREEN, GREEN, BRIGHT_GREEN, GREEN, BRIGHT_GREEN, GREEN, BRIGHT_GREEN, GREEN,BRIGHT_GREEN, GREEN,BRIGHT_GREEN, GREEN,BRIGHT_GREEN, GREEN, RESET);
    
    printf("%sVoice%s: %s%s%s\n",
        BLUE, RESET,voice_muted ? RED : GREEN, voice_muted?"muted":"unmuted", RESET
    );
    printf("%sMic%s: %s%s%s\n", 
        BLUE, RESET, mic_muted ? RED : GREEN, mic_muted?"muted":"unmuted", RESET
    );
}

// ---------------- KEYS HANDLING ----------------
void handle_keys() {
    static int prev_mute=0, prev_voice=0;
    int pressed_mute = is_hotkey_pressed(cfg.mute_key);
    int pressed_voice = is_hotkey_pressed(cfg.voice_mute_key);

    if (pressed_mute && !prev_mute) {
        mic_muted = !mic_muted;
        play_sound_async(mic_muted ? cfg.mic_mute_sound : cfg.mic_unmute_sound);
        print_status();
    }
    if (pressed_voice && !prev_voice) {
        voice_muted = !voice_muted;
        play_sound_async(voice_muted ? cfg.voice_mute_sound : cfg.voice_unmute_sound);
        print_status();
    }

    prev_mute = pressed_mute;
    prev_voice = pressed_voice;
}


// ---------------- MAIN ----------------
int main() {
    if (!parse_config("config.cfg",&cfg)) return 1;

    if (Pa_Initialize()!=paNoError) {
        fprintf(stderr,"Failed to init PortAudio\n"); return 1;
    }

    // Init Sockets
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cfg.server_port);
    inet_pton(AF_INET, cfg.server_ip, &servaddr.sin_addr);

    // Init Audio
    Pa_Initialize();
    Pa_OpenDefaultStream(&inputStream,
                         1, 0,
                         paInt16,
                         cfg.sample_rate,
                         cfg.frames_per_buffer,
                         NULL, NULL);

    Pa_OpenDefaultStream(&outputStream,
                         0, 1,
                         paInt16,
                         cfg.sample_rate,
                         cfg.frames_per_buffer,
                         NULL, NULL);

    Pa_StartStream(inputStream);
    Pa_StartStream(outputStream);

    // Threads
#ifdef _WIN32
    HANDLE t1 = CreateThread(NULL, 0, mic_thread, NULL, 0, NULL);
    HANDLE t2 = CreateThread(NULL, 0, speaker_thread, NULL, 0, NULL);
    HANDLE t3 = CreateThread(NULL, 0, key_thread, NULL, 0, NULL);
    WaitForSingleObject(t1, INFINITE);
    WaitForSingleObject(t2, INFINITE);
    WaitForSingleObject(t3, INFINITE);
#else
    pthread_t t1, t2, t3;
    pthread_create(&t1, NULL, mic_thread, NULL);
    pthread_create(&t2, NULL, speaker_thread, NULL);
    pthread_create(&t3, NULL, key_thread, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
#endif

    Pa_StopStream(inputStream);
    Pa_StopStream(outputStream);
    Pa_Terminate();

    CLOSESOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
    
    print_status();

    while(1) {
        handle_keys();
        Sleep(50);
    }

    Pa_Terminate();
    return 0;
}