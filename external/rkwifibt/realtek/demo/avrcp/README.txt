������ƵAVRCP API�ǻ���BLue5 �ڷ�װ��API��ʹ�����£�

1����libĿ¼�з���libgdbus-internal.a ע��32λϵͳ��ʹ��armĿ¼��libgdbus-internal.a, 64λϵͳ��ʹ��arm64Ŀ¼��libgdbus-internal.a

2����������
��Makefile ����������
 -lglib-2.0  -ldbus-1 -ldbus-glib-1  -lreadline
CFLAGS ����
 $(shell pkg-config --libs dbus-glib-1) $(shell pkg-config --libs glib-2.0) $(shell pkg-config --cflags dbus-1) 
 
3�����avrcpctrl.h avrcpctrl.c gdbus.h ��APP������