#ifndef __LED_TEST_H_
#define __LED_TEST_H_

#define AUDIO_PLAY_FILE "audio_test_start.wav"
#define AUDIO_RECORD_FILE "record_test.pcm"

void *audio_test(void *argv);  //¼�����������Գ���
void *audio_play_test(void *argv);  //��������
void *audio_record_test(void *argv);  //¼������


#define AUDIO_LINEIN_FILE "linein_test.pcm"
void *audio_line_in_test(void *argv);   //Line in ����

#endif
