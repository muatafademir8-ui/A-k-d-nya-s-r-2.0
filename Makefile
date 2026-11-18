TARGET = AcikDunya
OBJS = main.o
CFLAGS = -O2 -G0 -Wall -std=gnu99
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspgum -lpspgu -lm -lpspdebug -lpspdisplay -lpspge -lpspctrl
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Acik Dunya Surus
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
