# @author     dilfish (zhangpu@dnspod.com)
# @version    0.3
OBJS=utils.o datas.o net.o storage.o dns.o io.o event.o author.o init.o
LD=-lm -lc
TARGET_DIR=/home/p-dnspod/fish
ROOT_DIR=/root/fish

ifeq (${T},g)
CFLAGS=-g
else
CFLAGS=
endif

all:$(OBJS)
	gcc -o dnspod-sr $(LD) $(OBJS) -lpthread
#ltcmalloc


install:dnspod-sr src
	#scp ./dnspod-sr p-dnspod@dn1:$(TARGET_DIR)
	#scp ./src       p-dnspod@dn1:$(TARGET_DIR)
	#scp ./dnspod-sr p-dnspod@dn2:$(TARGET_DIR)
	#scp ./src       p-dnspod@dn2:$(TARGET_DIR)
	#scp ./dnspod-sr p-dnspod@dt1:$(TARGET_DIR)
	scp ./src       root@ktw:$(ROOT_DIR)
	scp ./dnspod-sr root@ktr:$(ROOT_DIR)
	#scp ./src       p-dnspod@dt2:$(TARGET_DIR)

cli:client.c
	gcc -o src client.c -lpthread
	cp src ../

#base 3
#misc,data,net
utils.o:utils.h utils.c
datas.o:utils.o datas.h datas.c
net.o:utils.o net.h net.c
storage.o:utils.o storage.h storage.c
#dns protocal,read from/write to config/log file
dns.o:datas.o net.o storage.o dns.h dns.c
io.o:dns.o io.h io.c
#event driven
event.o:net.o event.h event.c
author.o:io.o event.o author.c author.h
#start
init.o:author.o init.c

.PHONY : clean
.PHONY : show
show:
	make clean
	wc -l *.c *.h filter/*.c filter/*.h $(ADD) | sort -n
clean:
	rm -f $(OBJS) dnspod-sr src
