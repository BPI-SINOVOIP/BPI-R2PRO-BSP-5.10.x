tinymix set "ADC MIC Group 1 Left Volume" 3
tinymix set "ADC MIC Group 2 Left Volume" 3
tinymix set "ADC MIC Group 2 Right Volume" 3
tinymix set "ADC ALC Group 2 Left Volume" 18
tinymix set "ADC ALC Group 2 Right Volume" 18

amixer cset name='vad switch' 1
#echo 0x60 0x40ff0050 > /sys/kernel/debug/vad/reg
echo 0x60 0x40ff0190 > /sys/kernel/debug/vad/reg
echo 0x5c 0x000e2080 > /sys/kernel/debug/vad/reg

arecord -D 2mic_loopback -c 3  -r 16000 -f S16_LE -d 1 -t raw /tmp/test.pcm
rm /tmp/test.pcm                                                


ln -s /oem/aispeech_softap_lite /data/aispeech_softap_lite
ln -s /oem/wifi_monitor.sh /data/
ln -s /oem/dds_client /data/dds_client
ln -s /oem/dds_service.sh /data/

export  AISPEECH_WIFI_CFG="/data/wpa_supplicant.conf"

#aispeech dui app information file
export  AISPEECH_DUIKIT_APP="/data/aispeech_softap_lite/device/app.json"

#aispeech dui device file
export  AISPEECH_DUIKIT_DEVICE="/data/aispeech_softap_lite/device/device.json"

#aispeech dui softap web server address
export  AISPEECH_SOFTAP_SERVER_PORT="8000"

#aispeech dui softap configuration folder
export  AISPEECH_SOFTAP_DIR="/data/cfg"

export  AISPEECH_DO_CONNECT_MP3="/data/aispeech_softap_lite/audio/do_connect.mp3"

export  AISPEECH_WIFI_OK_MP3="/data/aispeech_softap_lite/audio/wifi_ok.mp3"

export  AISPEECH_NEED_CONNECT_MP3="/data/aispeech_softap_lite/audio/need_connect.mp3"

export  AISPEECH_CONNECT_OK_MP3="/data/aispeech_softap_lite/audio/connect_ok.mp3"

export  AISPEECH_START_CONNECT_MP3="/data/aispeech_softap_lite/audio/start_connect.mp3"

export  PATH=/bin:/sbin:/usr/bin:/usr/sbin:/userdata:/userdata/bin:/data/bin:/data/bin/rk_pcba_test:/data/aispeech_softap_lite/bin

if [ -f ${AISPEECH_WIFI_CFG} ]; then
    #aispeech_player ${AISPEECH_WIFI_OK_MP3}
    aispeech_led  -m on 6
    #wpa_supplicant -B -i wlan0 -c ${AISPEECH_WIFI_CFG} &
    wpa_supplicant -i wlan0 -c /data/cfg/wpa_supplicant.conf -dddddd > /tmp/wifi_log.txt&
    dhcpcd &
    aispeech_player ${AISPEECH_DO_CONNECT_MP3}
else
    aispeech_player ${AISPEECH_NEED_CONNECT_MP3} &
    cp /data/cfg/wpa_supplicant.conf /data/wpa_supplicant.conf &
    wpa_supplicant -i wlan0 -c /data/cfg/wpa_supplicant.conf -dddddd > /tmp/wifi_log.txt&
    softapServer Rockchip-Echo-123 &
    #aispeech_led -m breath 1 -s 500
    #ifconfig wlan0 down 
    #aispeech_softap_server -s aiengine -p 12345678 start &
fi
aispeech_startup &

/data/wifi_monitor.sh &
