#include <stdio.h>
#include <Windows.h>
#include <mmeapi.h>
#include <string.h>
#include <ctype.h>
#include "deepspeech.h"

#pragma warning(disable:4996)

#pragma comment(lib, "libdeepspeech.so.if.lib")
#pragma comment(lib, "winmm.lib")
#define NUM_FRAMES 6500

char RUN = 1;

char* skipws(unsigned char* str) {
  while (isspace(*str))
    if (*str) str++;
    else break;
  return str;
}

int getword(char** str) {
  if (!*str) {
    *str = NULL;
    return 0;
  }
  unsigned char* s = skipws(*str);
  int len = 0;
  while (!isspace(*s)) {
    if (!(*s++)) {
      *str = NULL;
      return len;
    }
    len++;
  }
  *str = s+1;
  return len;
}


void kb_wordn(char* word, int n) {
  const int B_SIZE = (2*n + 1) * sizeof(wchar_t);
  wchar_t* wword = malloc(B_SIZE);
  int len = MultiByteToWideChar(CP_UTF8, 0, word, n, wword, B_SIZE);

  INPUT ip;
  ip.type = INPUT_KEYBOARD;
  ip.ki.time = 0;
  ip.ki.wVk = 0;
  ip.ki.dwExtraInfo = 0;
  for (int i = 0; i < len; i++) {
    ip.ki.dwFlags = KEYEVENTF_UNICODE;
    ip.ki.wScan = wword[i];

    SendInput(1, &ip, sizeof(INPUT));
    ip.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(INPUT));
  }

  free(wword);

  ip.ki.dwFlags = KEYEVENTF_UNICODE;
  ip.ki.wScan = L' ';

  SendInput(1, &ip, sizeof(INPUT));
  ip.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  SendInput(1, &ip, sizeof(INPUT));
}

int main(int argc, char** argv)
{
  if (argc < 2) return 0;
  ModelState* model_;
  StreamingState* stream_;
  unsigned streamtime = 0;
  printf("MODEL: %d\n", DS_CreateModel(argv[1], &model_));
  printf("STREAM: %d", DS_CreateStream(model_, &stream_));
  if(argc > 2)
    if (DS_EnableExternalScorer(model_, argv[2]))
      fprintf(stderr, "Nie mozna otworzyc scorera!\n");
  HWAVEIN inStream;
  WAVEFORMATEX waveFormat;
  WAVEHDR buffer[2];

  waveFormat.wFormatTag = 1;
  waveFormat.nChannels = 1;
  waveFormat.nSamplesPerSec = 16000;
  waveFormat.nAvgBytesPerSec = 32000;
  waveFormat.nBlockAlign = 2;
  waveFormat.wBitsPerSample = 16;
  waveFormat.cbSize = 0;

  printf("%d\n", waveInOpen(&inStream, WAVE_MAPPER, &waveFormat, NULL,
    0, CALLBACK_NULL));

  for (int i = 0; i < 2; i++) {
    buffer[i].dwBufferLength = NUM_FRAMES * waveFormat.nBlockAlign;
    buffer[i].lpData = malloc(NUM_FRAMES * waveFormat.nBlockAlign);
    buffer[i].dwFlags = 0;
  }
  waveInPrepareHeader(inStream, buffer, sizeof(WAVEHDR));
  waveInPrepareHeader(inStream, buffer + 1, sizeof(WAVEHDR));
  
  waveInAddBuffer(inStream, buffer, sizeof(WAVEHDR));
  waveInStart(inStream);

  char cbuff = 0;
  char* laststr = NULL;
  unsigned writtennum = 0;
  unsigned short nochg = 0;
  while (RUN) {
    if (buffer[cbuff].dwFlags & WHDR_DONE) {
      waveInAddBuffer(inStream, &buffer[!cbuff], sizeof(WAVEHDR));
      
      DS_FeedAudioContent(stream_, buffer[cbuff].lpData,
        buffer[cbuff].dwBytesRecorded / 2);
      char* cstr = DS_IntermediateDecode(stream_);
      char *s1 = laststr, *s2 = cstr;
      unsigned li = 0;
      do {
        char *c1 = s1, *c2 = s2;
        int w1len = getword(&s1);
        int w2len = getword(&s2);
        if (!w1len || !w2len)
          break;
        if(li >= writtennum)
          if (w1len == w2len && !memcmp(c1, c2, w1len)) {
            printf("WRITE: %.*s\n", w1len, c1);
            kb_wordn(c1, w1len);
            writtennum++;
          }
        li++;
      } while (s1);
      if (laststr && !strcmp(cstr, laststr)) {
        nochg++;
        if (streamtime > 75 && nochg > 5) {
          DS_FreeString(DS_FinishStream(stream_));
          int result = DS_CreateStream(model_, &stream_);
          printf("%d\n", result);
          nochg = 0;
          streamtime = 0;
          writtennum = 0;
        }
      } else nochg = 0;
      printf("out: %s\n", cstr);
     /*FILE* handle = fopen("out.txt", "w");
      fprintf(handle, "%s", cstr);
      fclose(handle);*/
      cbuff = !cbuff;

      DS_FreeString(laststr);
      laststr = cstr;
      streamtime++;
    }
    Sleep(25);
  }
  DS_FreeModel(model_);
  return 0;
}
