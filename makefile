.DELETE_ON_ERROR:
.SECONDARY:

dpkg := dpkg-deb -Zlzma
version := $(shell ./version.sh)

flag := 
plus :=
link := 
libs := 
lapt := 

ifeq ($(doIA),yes)
kind := iphonesimulator
arch := x86_64
else
kind := iphoneos
arch := arm64
endif

gxx := $(shell xcrun --sdk $(kind) -f g++)
cycc := $(gxx)

sdk := $(shell xcodebuild -sdk $(kind) -version Path)
mac := $(shell xcodebuild -sdk macosx -version Path)

cycc += -isysroot $(sdk)
cycc += -idirafter $(mac)/usr/include
cycc += -F$(sdk)/System/Library/PrivateFrameworks

ifeq ($(doIA),yes)
cycc += -Xarch_x86_64 -F$(sdk)/../../../../iPhoneOS.platform/Developer/Library/CoreSimulator/Profiles/Runtimes/iOS.simruntime/Contents/Resources/RuntimeRoot/System/Library/PrivateFrameworks
endif

cycc += -include system.h

cycc += -fmessage-length=0
cycc += -gfull -O2
cycc += -fvisibility=hidden

link += -Wl,-dead_strip
link += -Wl,-no_dead_strip_inits_and_terms

iapt := 
iapt += -Iapt32
iapt += -Iapt32-contrib
iapt += -Iapt32-deb
iapt += -Iapt-extra
iapt += -IObjects/apt32

ifeq ($(do32),yes)
flag += $(patsubst %,-Xarch_armv6 %,$(iapt))
endif

flag += $(patsubst %,-Xarch_$(arch) %,$(subst apt32,apt64,$(iapt)))

flag += -I.
flag += -isystem sysroot/usr/include

flag += -idirafter icu/icuSources/common
flag += -idirafter icu/icuSources/i18n

flag += -Wall
flag += -Wno-dangling-else
flag += -Wno-deprecated-declarations
flag += -Wno-objc-protocol-method-implementation
flag += -Wno-logical-op-parentheses
flag += -Wno-shift-op-parentheses
flag += -Wno-unknown-pragmas
flag += -Wno-unknown-warning-option

plus += -fobjc-call-cxx-cdtors
plus += -fvisibility-inlines-hidden

link += -multiply_defined suppress

libs += -framework CoreFoundation
libs += -framework CoreGraphics
libs += -framework Foundation
libs += -framework GraphicsServices
libs += -framework IOKit
libs += -framework QuartzCore
libs += -framework SpringBoardServices
libs += -framework SystemConfiguration
libs += -framework WebKit

libs += -framework CFNetwork

ifeq ($(do32),yes)
libs += -framework WebCore
libs += -llockdown
libs += -Xarch_armv6 -Wl,-force_load,Objects/libapt32.a
lapt += Objects/libapt32.a
endif

libs += -Xarch_$(arch) -Wl,-force_load,Objects/libapt64.a
lapt += Objects/libapt64.a

libs += -licucore

uikit := 
uikit += -framework UIKit

dirs := Menes CyteKit Cydia SDURLCache

code := $(foreach dir,$(dirs),$(wildcard $(foreach ext,h hpp c cpp m mm,$(dir)/*.$(ext))))
code := $(filter-out SDURLCache/SDURLCacheTests.m,$(code))
code += MobileCydia.mm Version.mm iPhonePrivate.h Cytore.hpp lookup3.c Sources.h Sources.mm DiskUsage.cpp

code += gpgv.cc
code += http.cc

source := $(filter %.m,$(code)) $(filter %.mm,$(code))
source += $(filter %.c,$(code)) $(filter %.cpp,$(code)) $(filter %.cc,$(code))
header := $(filter %.h,$(code)) $(filter %.hpp,$(code)) $(filter %.hh,$(code))

object := $(source)
object := $(object:.c=.o)
object := $(object:.cpp=.o)
object := $(object:.cc=.o)
object := $(object:.m=.o)
object := $(object:.mm=.o)
object := $(object:%=Objects/%)

methods := copy file rred

libapt32 := 
libapt32 += $(wildcard apt32/apt-pkg/*.cc)
libapt32 += $(wildcard apt32/apt-pkg/deb/*.cc)
libapt32 += $(wildcard apt32/apt-pkg/contrib/*.cc)
libapt32 += apt32/methods/gzip.cc
libapt32 += $(patsubst %,apt32/methods/%.cc,$(methods))
libapt32 := $(patsubst %.cc,Objects/%.o,$(libapt32))

libapt64 := 
libapt64 += $(wildcard apt64/apt-pkg/*.cc)
libapt64 += $(wildcard apt64/apt-pkg/deb/*.cc)
libapt64 += $(wildcard apt64/apt-pkg/contrib/*.cc)
libapt64 += apt64/apt-pkg/tagfile-keys.cc
libapt64 += apt64/methods/store.cc
libapt64 += $(patsubst %,apt64/methods/%.cc,$(methods))
libapt64 := $(filter-out %/srvrec.cc,$(libapt64))
libapt64 := $(patsubst %.cc,Objects/%.o,$(libapt64))

link += -Wl,-liconv
link += -Xarch_$(arch) -Wl,-lz

flag += -DAPT_PKG_EXPOSE_STRING_VIEW
flag += -Dsighandler_t=sig_t

ifeq ($(do32),yes)
flag32 := 
flag32 += -arch armv6
flag32 += -Xarch_armv6 -miphoneos-version-min=2.0
flag32 += -Xarch_armv6 -marm # @synchronized
flag32 += -Xarch_armv6 -mcpu=arm1176jzf-s
flag32 += -mllvm -arm-reserve-r9

link += -Xarch_armv6 -Wl,-lgcc_s.1
link += -Xarch_armv6 -Wl,-segalign,4000

apt32 := $(cycc) $(flag32) $(flag)
apt32 += -Wno-deprecated-register
apt32 += -Wno-format-security
apt32 += -Wno-tautological-compare
apt32 += -Wno-uninitialized
apt32 += -Wno-unused-private-field
apt32 += -Wno-unused-variable
endif

flag64 := 
flag64 += -arch $(arch)
flag64 += -Xarch_$(arch) -m$(kind)-version-min=7.0

apt64 := $(cycc) $(flag64) $(flag)
apt64 += -include apt.h
apt64 += -Wno-deprecated-register
apt64 += -Wno-unused-private-field
apt64 += -Wno-unused-variable

eapt := -include apt.h
apt64 += $(eapt)
eapt += -D'VERSION="0.7.25.3"'
apt32 += $(eapt)
eapt += -Wno-format
eapt += -Wno-logical-op-parentheses
iapt += $(eapt)

ifeq ($(do32),yes)
cycc += $(flag32)
endif

cycc += $(flag64)

plus += -std=c++11

images := $(shell find MobileCydia.app/ -type f -name '*.png')
images := $(images:%=Images/%)

lproj_deb := debs/cydia-lproj_$(version)_iphoneos-arm.deb

all: MobileCydia

clean:
	rm -f MobileCydia postinst
	rm -rf Objects/ Images/

Objects/apt64/apt-pkg/tagfile.o: Objects/apt64/apt-pkg/tagfile-keys.h
Objects/apt64/apt-pkg/deb/deblistparser.o: Objects/apt64/apt-pkg/tagfile-keys.h

Objects/apt64/apt-pkg/tagfile-keys%h apt64/apt-pkg/tagfile-keys%cc:
	mkdir -p apt64
	mkdir -p Objects/apt64/apt-pkg
	cd apt64 && ../apt64/triehash/triehash.pl \
            --ignore-case \
            --header ../Objects/apt64/apt-pkg/tagfile-keys.h \
            --code apt-pkg/tagfile-keys.cc \
            --enum-class \
            --enum-name pkgTagSection::Key \
            --function-name pkgTagHash \
            --include "<apt-pkg/tagfile.h>" \
            ../apt64/apt-pkg/tagfile-keys.list
	sed -i -e 's@typedef char static_assert64@//\\0@' apt64/apt-pkg/tagfile-keys.cc

Objects/%.o: %.cc $(header)
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(cycc) $(plus) -c -o $@ $< $(flag) -Wno-format -include apt.h -Dmain=main_$(basename $(notdir $@))

Objects/apt32/%.o: apt32/%.cc $(header) apt.h apt-extra/*.h
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(apt32) -c -o $@ $< -Dmain=main_$(basename $(notdir $@))

Objects/apt64/%.o: apt64/%.cc $(header) apt.h apt-extra/*.h
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(apt64) $(plus) -c -o $@ $< -Dmain=main_$(basename $(notdir $@))

Objects/%.o: %.c $(header)
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(cycc) -c -o $@ -x c $< $(flag)

Objects/%.o: %.m $(header)
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(cycc) -c -o $@ $< $(flag)

Objects/%.o: %.cpp $(header)
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(cycc) $(plus) -c -o $@ $< $(flag)

Objects/%.o: %.mm $(header)
	@mkdir -p $(dir $@)
	@echo "[cycc] $<"
	@$(cycc) $(plus) -c -o $@ $< $(flag)

Objects/Version.o: Version.h

Images/%.png: %.png
	@mkdir -p $(dir $@)
	@echo "[pngc] $<"
	@./pngcrush.sh $< $@
	@touch $@

sysroot: sysroot.sh
	@echo "Your ./sysroot/ is either missing or out of date. Please read compiling.txt for help." 1>&2
	@echo 1>&2
	@exit 1

Objects/libapt32.a: $(libapt32)
	@echo "[arch] $@"
	@ar -rc $@ $^

Objects/libapt64.a: $(libapt64)
	@echo "[arch] $@"
	@ar -rc $@ $^

MobileCydia: $(object) entitlements.xml $(lapt)
	@echo "[link] $@"
	@$(cycc) -o $@ $(filter %.o,$^) $(link) $(libs) $(uikit) -Wl,-sdk_version,8.0
	@mkdir -p bins
	@cp -a $@ bins/$@-$(version)_$(shell date +%s)
	@echo "[strp] $@"
	@grep '~' <<<"$(version)" >/dev/null && echo "skipping..." || strip $@
	@echo "[uikt] $@"
	@./uikit.sh $@
	@echo "[sign] $@"
	@ldid -T0 -Sentitlements.xml $@ || { rm -f $@ && false; }

cfversion: cfversion.mm
	$(cycc) -o $@ $(filter %.mm,$^) $(flag) $(link) -framework CoreFoundation
	@ldid -T0 -S $@

setnsfpn: setnsfpn.cpp
	$(cycc) -o $@ $(filter %.cpp,$^) $(flag) $(link)
	@ldid -T0 -S $@

cydo: cydo.cpp
	$(cycc) $(plus) -o $@ $(filter %.cpp,$^) $(flag) $(link) -Wno-deprecated-writable-strings
	@ldid -T0 -S $@

postinst: postinst.mm CyteKit/stringWith.mm CyteKit/stringWith.h CyteKit/UCPlatform.h
	$(cycc) $(plus) -o $@ $(filter %.mm,$^) $(flag) $(link) -framework CoreFoundation -framework Foundation -framework UIKit
	@ldid -T0 -S $@

debs/cydia_$(version)_iphoneos-arm.deb: MobileCydia preinst postinst cfversion setnsfpn cydo $(images) $(shell find MobileCydia.app) cydia.control Library/firmware.sh Library/move.sh Library/startup
	sudo rm -rf _
	mkdir -p _/var/lib/cydia
	
	mkdir -p _/etc/apt
	mkdir _/etc/apt/apt.conf.d
	mkdir _/etc/apt/preferences.d
	cp -a Trusted.gpg _/etc/apt/trusted.gpg.d
	cp -a Sources.list _/etc/apt/sources.list.d
	
	mkdir -p _/usr/libexec
	cp -a Library _/usr/libexec/cydia
	cp -a sysroot/usr/bin/du _/usr/libexec/cydia
	cp -a cfversion _/usr/libexec/cydia
	cp -a setnsfpn _/usr/libexec/cydia
	
	cp -a cydo _/usr/libexec/cydia
	
	mkdir -p _/Library
	cp -a LaunchDaemons _/Library/LaunchDaemons
	
	mkdir -p _/Applications
	cp -a MobileCydia.app _/Applications/Cydia.app
	rm -rf _/Applications/Cydia.app/*.lproj
	cp -a MobileCydia _/Applications/Cydia.app/Cydia
	
	for meth in bzip2 gzip lzma gpgv http https store $(methods); do ln -s Cydia _/Applications/Cydia.app/"$${meth}"; done
	
	cd MobileCydia.app && find . -name '*.png' -exec cp -af ../Images/MobileCydia.app/{} ../_/Applications/Cydia.app/{} ';'
	
	mkdir -p _/Applications/Cydia.app/Sources
	ln -s /usr/share/bigboss/icons/bigboss.png _/Applications/Cydia.app/Sources/apt.bigboss.us.com.png
	ln -s /usr/share/bigboss/icons/planetiphones.png _/Applications/Cydia.app/Sections/"Planet-iPhones Mods.png"
	
	mkdir -p _/DEBIAN
	./control.sh cydia.control _ >_/DEBIAN/control
	cp -a preinst postinst _/DEBIAN/
	
	find _ -exec touch -t "$$(date -j -f "%s" +"%Y%m%d%H%M.%S" "$$(git show --format='format:%ct' | head -n 1)")" {} ';'
	
	sudo chown -R 0 _
	sudo chgrp -R 0 _
	sudo chmod 6755 _/usr/libexec/cydia/cydo
	
	mkdir -p debs
	ln -sf debs/cydia_$(version)_iphoneos-arm.deb Cydia.deb
	$(dpkg) -b _ Cydia.deb
	@echo "$$(stat -L -f "%z" Cydia.deb) $$(stat -f "%Y" Cydia.deb)"

$(lproj_deb): $(shell find MobileCydia.app -name '*.strings') cydia-lproj.control
	sudo rm -rf __
	mkdir -p __/Applications/Cydia.app
	
	cp -a MobileCydia.app/*.lproj __/Applications/Cydia.app
	
	mkdir -p __/DEBIAN
	./control.sh cydia-lproj.control __ >__/DEBIAN/control
	
	sudo chown -R 0 __
	sudo chgrp -R 0 __
	
	mkdir -p debs
	ln -sf debs/cydia-lproj_$(version)_iphoneos-arm.deb Cydia_.deb
	$(dpkg) -b __ Cydia_.deb
	@echo "$$(stat -L -f "%z" Cydia_.deb) $$(stat -f "%Y" Cydia_.deb)"
	
package: debs/cydia_$(version)_iphoneos-arm.deb $(lproj_deb)

.PHONY: all clean package
