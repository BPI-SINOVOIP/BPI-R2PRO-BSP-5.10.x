libbdSPILAudioProc.so
md5:e5d3bf87f8c39dbf43b707227564317e

libbd_audio_vdev.so
md5:f7ab4d038d31313c84ea4416a558f41e

libbd_alsa_audio_client.so
md5:c68fe39322647b4bf8bb576ad19bb707

���ɺ�ʹ��˵����
1. push�⵽�豸��
   adb push libbdSPILAudioProc.so /data
   adb push libbd_audio_vdev.so /data
   adb push libbd_alsa_audio_client.so /data
   adb push config_huamei_rk3308_4_1.lst /data
   adb push setup.sh /data
   adb push alsa_audio_main_service /data
   adb push alsa_audio_client_sample /data
   adb shell sync
2. ����Ŀ¼���޸�Ȩ��
   adb shell;cd /data
   chmod +x alsa_audio_*
   chmod +x setup.sh
   mkdir -p /data/local/ipc
   chmod 777 /data/local/ipc
3. ����main service
   ./setup.sh
   ./alsa_audio_main_service hw:0,0 &
   hw:0,0�Ƕ�Ӧ��¼���豸�������ź�device�ţ�Ҳ��������asound.conf��ʹ���߼�pcm�豸��
4. ����app,����duer_linux, ��Ҫ���/dataĿ¼��duer_linux�Ķ�̬������·����
   Ҳ�����������ǵ�sample����
   ./alsa_audio_client_sample
   �ڵ�ǰĿ¼�»ᱣ�澭���źŴ����¼���ļ�dump_pcm.pcm����˫������16K��С�ˣ�16bitλ����Ƶ��

����ԭʼ¼�����ݵķ���:
	����¼��ǰ���У�
	mkdir -p /data/local/aw.so_profile
	touch  /data/local/aw.so_profile/dump_switch
	touch /data/local/aw.so_profile/dump_switch_wakets
	mkdir -p /data/local/aud_rec/
	chmod 777 /data/local/aud_rec/
��/data/local/aud_recĿ¼�»ᱣ��4·��˷����ݺ�1·�ο����ݣ�һ·ʶ�����ݣ�һ·�������ݡ�
�ļ������ݸ�ʽ���ǣ� 16KHz��С�ˡ�16bit��������
   