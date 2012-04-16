 GRDIR = /usr/local/gr
  DIRS = lib/gr lib/gr3

UNAME := $(shell uname)

default:
	@make `lib/Preflight`
nothing:
	@true

Linux: all
Darwin: all

all:
	@for d in $(DIRS); do make -C $$d GRDIR=$(GRDIR); done
ifeq ($(UNAME), Darwin)
# -arch i386 for xcode 4.3 or later
	xcodebuild -project lib/gks/quartz/GKSTerm.xcodeproj ARCHS=i386
endif

install: default
	@for d in $(DIRS); do make -C $$d GRDIR=$(GRDIR) install; done
ifeq ($(UNAME), Darwin)
	@if [ ! -d $(GRDIR)/Applications ]; then \
	mkdir -m 755 $(GRDIR)/Applications; fi
	@ditto lib/gks/quartz/build/Release/GKSTerm.app \
	$(GRDIR)/Applications/GKSTerm.app 
endif

clean:
	@for d in $(DIRS); do make -C $$d clean; done
ifeq ($(UNAME), Darwin)
	xcodebuild -project lib/gks/quartz/GKSTerm.xcodeproj clean
endif