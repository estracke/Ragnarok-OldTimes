all txt: char-server

COMMON_OBJ = ../common/obj/core.o ../common/obj/socket.o ../common/obj/timer.o \
	../common/obj/db.o ../common/obj/plugins.o ../common/obj/lock.o \
	../common/obj/malloc.o ../common/obj/showmsg.o ../common/obj/utils.o \
	../common/obj/strlib.o ../common/obj/graph.o ../common/obj/grfio.o ../zlib/unz.o
COMMON_H = ../common/core.h ../common/socket.h ../common/timer.h ../common/mmo.h \
	../common/version.h ../common/db.h ../common/plugins.h ../common/lock.h \
	../common/malloc.h ../common/showmsg.h ../common/utils.h ../common/strlib.h \
	../common/graph.h ../common/grfio.h

%.o: %.c
	$(COMPILE.c) -DTXT_ONLY $(OUTPUT_OPTION) $<
	
char-server: char.o inter.o int_party.o int_guild.o int_status.o int_storage.o int_pet.o $(COMMON_OBJ)
	$(CC) -o ../../$@ $> $(LIB_S)

clean:
	rm -f *.o ../../char-server

# DO NOT DELETE

char.o: char.c char.h inter.h int_pet.h $(COMMON_H) ../common/version.h
inter.o: inter.c inter.h int_party.h int_guild.h int_storage.h int_pet.h char.h $(COMMON_H)
int_party.o: int_party.c int_party.h inter.h char.h $(COMMON_H)
int_guild.o: int_guild.c int_guild.h int_storage.h inter.h char.h $(COMMON_H)
int_storage.o: int_storage.c int_storage.h int_guild.h inter.h char.h $(COMMON_H)
int_status.o: int_status.c int_status.h char.h $(COMMON_H)
int_pet.o: int_pet.c int_pet.h inter.h char.h $(COMMON_H)
